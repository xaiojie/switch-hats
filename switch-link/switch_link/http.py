import json
from contextlib import contextmanager
import mimetypes
from pathlib import Path
import posixpath
import shutil
import threading
import time
import urllib.parse
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import __version__
from .discovery import Discovery, local_addresses
from .jobs import JobManager
from .library import Library, LibraryError
from .http_jobs import HttpInstallJobs, HttpJobError
from .music import MusicLibrary, MusicError, MAX_TAG
from .transport import RemoteError, RemoteFTP, validate_name, validate_path


class ApiError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


class Application:
    def __init__(self, data_dir, seeds=(), discover=True, library_root=None):
        self.data_dir = Path(data_dir)
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.library = Library(library_root or self.data_dir / 'library')
        self.music = MusicLibrary(self.data_dir / 'music')
        self.http_share = None
        self.control_share = None
        self.settings_path = self.data_dir / 'settings.json'
        self.selected_id = None
        self.selection_lock = threading.RLock()
        self.receiving_count = 0
        try:
            settings = json.loads(self.settings_path.read_text(encoding='utf-8'))
            self.selected_id = settings.get('selected_id')
            seeds = list(seeds) + [settings['last_ip']]
        except (OSError, ValueError, KeyError):
            pass
        self.discovery = Discovery(seeds, auto=discover)
        self.ftp_lock = threading.RLock()
        self.jobs = JobManager(self.transport, self.data_dir)
        self.http_jobs = HttpInstallJobs(self.library, auto=discover)
        self.discovery.busy = lambda: self.jobs.active() or self.http_jobs.active() or self.receiving_count > 0
        self.discovery.on_advertisement = self.http_jobs.observe
        self.port = 8088
        self.host = '127.0.0.1'
        self.stop = threading.Event()
        self.health_thread = None
        if discover:
            self.health_thread = threading.Thread(target=self._health, daemon=True)
            self.health_thread.start()

    def _health(self):
        while not self.stop.wait(8):
            device = self.discovery.get(self.selected_id)
            if device and not self.jobs.active() and not self.http_jobs.active():
                self.discovery.refresh(device)

    def selected(self):
        device = self.discovery.get(self.selected_id)
        if device:
            for job in self.jobs.snapshot() + self.http_jobs.snapshot():
                if (job['state'] in ('transferring', 'completed') and job.get('bytes_done', 0) > 0
                        and time.time() - job.get('updated_at', 0) < 20):
                    device['connected'] = True
                    break
        return device

    def require_device(self):
        device = self.selected()
        if not device or not device['connected']:
            raise ApiError(409, '请先连接一台已发现的 Switch')
        return device

    def require_idle(self):
        with self.selection_lock:
            if self.jobs.active() or self.http_jobs.active() or self.receiving_count:
                raise ApiError(409, '任务正在进行，请等待完成后操作文件或切换设备')

    def transport(self):
        device = self.require_device()
        return RemoteFTP(device['ip'], device['port'], sd_root=device.get('sd_root', '/'),
                         backup_dir=self.data_dir / 'backups', device=device, lock=self.ftp_lock)

    def assert_selected(self, expected):
        if not isinstance(expected, str) or not expected or expected != self.selected_id:
            raise ApiError(409, '主机已切换，请刷新页面后重新确认操作')

    @contextmanager
    def bound_transport(self, expected):
        with self.selection_lock:
            self.assert_selected(expected)
            self.require_idle()
            yield self.transport()

    def connect(self, device_id):
        self.require_idle()
        device = self.discovery.get(device_id)
        if not device:
            raise ApiError(404, '设备已不在发现列表，请重新搜索')
        device = self.discovery.refresh(device)
        if not device:
            raise ApiError(409, 'Switch 当前不可达，请保持客户端打开')
        with self.selection_lock:
            self.require_idle()
            self.selected_id = device['id']
        temporary = self.settings_path.with_suffix('.tmp')
        temporary.write_text(json.dumps({'selected_id': self.selected_id, 'last_ip': device['ip']}), encoding='utf-8')
        temporary.replace(self.settings_path)
        return device

    def status(self):
        addresses = local_addresses()
        device = self.selected()
        jobs = self.jobs.snapshot() + self.http_jobs.snapshot()
        task = (device or {}).get('current_task')
        if isinstance(task, dict) and task.get('source') == 'console':
            for job in jobs:
                if job['id'] == task.get('id') and job['state'] == 'transferring':
                    job['native_stage'] = task.get('stage')
                    for key in ('progress', 'bytes_done', 'bytes_total', 'speed'):
                        job['native_' + key] = task.get(key)
                    if (job.get('kind') == 'install' and task.get('stage') in ('installing', 'registering')
                            and job.get('bytes_total', 0) > 0
                            and job.get('bytes_done', 0) >= job['bytes_total']):
                        job['stage'] = task['stage']
                        job['eta_seconds'] = task.get('eta_seconds')
        return {'app': 'Switch Link', 'version': __version__,
                'lan_url': 'http://%s:%d/' % (self.host, self.port),
                'device': device,
                'discovery': {'running': self.discovery.running, 'error': self.discovery.error},
                'jobs': jobs, 'http_library': self.library_status()}

    def library_status(self):
        status = self.http_share.snapshot() if self.http_share else {
            'available': False, 'url': '', 'config': '', 'transfers': [],
            'switch_access': None, 'progress_available': False, 'error': 'HTTP 共享服务尚未启动'}
        return dict(status, library_id=self.library.id, protocol='http', can_browse_sd=False,
                    files=self.library.files(), can_publish=True,
                    native_install_available=bool(self.http_jobs.base_url),
                    native_install_error='' if self.http_jobs.base_url else 'Switch Link 主机 HTTP 安装通道尚未启动')

    def music_status(self):
        share = next((s for s in (self.http_share, self.control_share) if s and s.owned and s.music is self.music), None)
        return dict(self.music.snapshot(), available=bool(share),
                    catalog_url=share.url + '/music-library.json' if share else '',
                    m3u_url=share.url + '/music-list.m3u8' if share else '')

    def close(self):
        self.stop.set()
        self.discovery.close()
        self.jobs.close()
        self.http_jobs.close()
        if self.control_share:
            self.control_share.close()
        if self.http_share:
            self.http_share.close()
        if self.health_thread:
            self.health_thread.join(timeout=2)


def make_server(app, host, port, static_dir=None):
    static_root = Path(static_dir or Path(__file__).resolve().parent.parent / 'static')

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.0'

        def log_message(self, *args):
            pass

        def reply(self, payload, status=200):
            # On Windows closing a rejected request with unread body bytes can
            # reset the socket before the client receives the JSON error.
            if status >= 400 and 0 < getattr(self, 'body_remaining', 0) <= 65536:
                previous_timeout = self.connection.gettimeout()
                try:
                    self.connection.settimeout(.25)
                    self.read(self.body_remaining)
                except OSError:
                    pass
                finally:
                    self.connection.settimeout(previous_timeout)
            body = json.dumps(payload, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            try:
                self.end_headers()
                self.wfile.write(body)
            except ConnectionError:
                pass

        def read(self, size):
            chunk = self.rfile.read(size)
            self.body_remaining = max(0, self.body_remaining - len(chunk))
            return chunk

        def data(self, limit=262144):
            try:
                size = int(self.headers.get('Content-Length', 0))
                if not 0 <= size <= limit:
                    raise ValueError()
                value = json.loads(self.read(size) or b'{}')
                if not isinstance(value, dict):
                    raise ValueError()
                return value
            except (ValueError, TypeError):
                raise ApiError(400, '请求内容不合法')

        def do_GET(self):
            self.route('GET')

        def do_POST(self):
            self.route('POST')

        def do_PUT(self):
            self.route('PUT')

        def do_DELETE(self):
            self.route('DELETE')

        def route(self, method):
            receiving = False
            try:
                self.body_remaining = max(0, int(self.headers.get('Content-Length', 0)))
            except ValueError:
                self.body_remaining = 0
            try:
                if method != 'GET':
                    origin = self.headers.get('Origin')
                    if origin and urllib.parse.urlsplit(origin).netloc != self.headers.get('Host'):
                        raise ApiError(403, '已拒绝来自其他网站的操作请求')
                parsed = urllib.parse.urlsplit(self.path)
                path = urllib.parse.unquote(parsed.path)
                query = urllib.parse.parse_qs(parsed.query)
                arg = lambda name, default='': (query.get(name) or [default])[0]
                if method == 'GET' and path == '/api/music':
                    return self.reply(app.music_status())
                if method == 'PUT' and path == '/api/music':
                    data = self.data()
                    app.music.configure(data.get('root'), data.get('expected_library'))
                    return self.reply(app.music_status())
                if path in ('/api/music/sources', '/api/music/overrides'):
                    key = path.rsplit('/', 1)[1]
                    if method == 'GET': return self.reply(getattr(app.music, key))
                    if method == 'PUT':
                        data = self.data(4 * 1024 * 1024)
                        with app.music.lock:
                            app.music.assert_id(data.pop('expected_library', None))
                            return self.reply(getattr(app.music, 'set_' + key)(data))
                if method == 'PUT' and path == '/api/music/asset':
                    size = int(self.headers.get('Content-Length', 0))
                    if not 0 < size <= MAX_TAG: raise ApiError(413, '封面或歌词超过大小上限')
                    content = self.read(size)
                    if len(content) != size: raise ApiError(400, '资源接收中断')
                    href = app.music.upload_asset(arg('kind'), content, arg('expected_library'))
                    return self.reply({'href': href}, 201)
                if method == 'GET' and path == '/api/music/sd-library':
                    with app.bound_transport(arg('expected_device')) as peer:
                        if not any(e['name'] == 'config' for e in peer.list('/')) or not any(e['name'] == 'papa-walkman' for e in peer.list('/config')):
                            return self.reply({'version': 1, 'tracks': [], 'available': False})
                        entries = peer.list('/config/papa-walkman')
                        if not any(e['name'] == 'sd-library.json' for e in entries):
                            return self.reply({'version': 1, 'tracks': [], 'available': False})
                        received = bytearray()
                        def collect(chunk):
                            if len(received) + len(chunk) > 4 * 1024 * 1024: raise MusicError('SD 索引超过读取上限')
                            received.extend(chunk)
                        peer.download('/config/papa-walkman/sd-library.json', collect)
                        catalog = json.loads(received)
                        if catalog.get('version') != 1 or not isinstance(catalog.get('tracks'), list): raise MusicError('SD 索引格式不合法')
                        return self.reply(dict(catalog, available=True))
                if method == 'POST' and path == '/api/music/sync':
                    data = self.data()
                    with app.bound_transport(data.get('expected_device')) as peer:
                        with app.music.lock, app.ftp_lock:
                            app.music.assert_id(data.get('expected_library'))
                            url = app.music_status()['catalog_url']
                            results = app.music.sync(peer, url.rsplit('/', 1)[0] if url else '')
                    return self.reply({'ok': True, 'files': results})
                if method == 'GET' and path == '/api/status':
                    return self.reply(app.status())
                if method == 'GET' and path == '/api/devices':
                    return self.reply(app.discovery.snapshot())
                if method == 'GET' and path == '/api/jobs':
                    return self.reply({'jobs': app.jobs.snapshot() + app.http_jobs.snapshot()})
                if method == 'POST' and path == '/api/http-install':
                    data = self.data()
                    with app.selection_lock:
                        app.assert_selected(data.get('expected_device'))
                        device = app.require_device()
                        if app.jobs.active() or app.receiving_count:
                            raise ApiError(409, '请等待当前文件传输结束后再开始安装')
                        job = app.http_jobs.submit(device, data.get('name'))
                    return self.reply({'ok': True, 'job': job}, 202)
                if method == 'GET' and path == '/api/library':
                    return self.reply(app.library_status())
                if method == 'PUT' and path == '/api/install':
                    raise ApiError(410, '游戏安装已改用 HTTP。请将安装包加入 HTTP 游戏库，再在 DBI 的 SwitchLibrary 中选择安装。')
                if method == 'PUT' and path == '/api/library/upload':
                    if arg('expected_library') != app.library.id:
                        raise ApiError(409, '游戏库已变化，请刷新页面后重新选择安装包')
                    try:
                        size = int(self.headers.get('Content-Length', 0))
                    except ValueError:
                        raise ApiError(411, '文件大小不合法')
                    entry = app.library.receive(arg('name'), self, size)
                    return self.reply({'ok': True, 'file': entry,
                                       'message': '已加入 HTTP 游戏库，请在 DBI 的 SwitchLibrary 中选择安装'}, 201)
                if method == 'POST' and path == '/api/discover':
                    app.require_idle()
                    app.discovery.scan()
                    return self.reply({'ok': True}, 202)
                if method == 'POST' and path == '/api/connect':
                    return self.reply({'ok': True, 'device': app.connect(self.data().get('id'))})
                if method == 'POST' and path == '/api/jobs/cancel':
                    job_id = self.data().get('id')
                    if any(j['id'] == job_id for j in app.http_jobs.snapshot()):
                        app.http_jobs.cancel(job_id)
                    else:
                        app.jobs.cancel(job_id)
                    return self.reply({'ok': True})
                if method == 'GET' and path == '/api/files':
                    current = validate_path(arg('path', '/'))
                    with app.bound_transport(arg('expected_device')) as peer:
                        return self.reply({'path': current, 'entries': peer.list(current),
                                           'device_id': app.selected_id})
                if method == 'PUT' and path == '/api/files/upload':
                    with app.selection_lock:
                        if app.http_jobs.active():
                            raise ApiError(409, 'HTTP 安装任务正在进行，请等待结束后再向 SD 卡上传文件')
                        device = app.require_device()
                        app.assert_selected(arg('expected_device'))
                        app.receiving_count += 1
                        receiving = True
                    name = validate_name(arg('name'))
                    if device.get('can_browse') is False:
                        raise ApiError(409, 'DBI 当前是游戏安装模式。上传普通文件请切换到「浏览 SD 卡」。')
                    target = posixpath.join(validate_path(arg('path', '/')), name)
                    try:
                        size = int(self.headers.get('Content-Length', 0))
                    except ValueError:
                        size = 0
                    if not 0 < size < 128 * 1024 ** 3:
                        raise ApiError(411, '文件大小不合法或文件为空')
                    if shutil.disk_usage(app.data_dir).free < size + 128 * 1024 ** 2:
                        raise ApiError(507, '电脑暂存空间不足，请先释放空间')
                    inbox = app.data_dir / 'inbox'
                    inbox.mkdir(exist_ok=True)
                    source = inbox / (uuid.uuid4().hex + '.bin')
                    remaining = size
                    try:
                        with source.open('wb') as output:
                            while remaining:
                                chunk = self.read(min(1024 * 1024, remaining))
                                if not chunk:
                                    raise ApiError(400, '文件接收中断，请重新上传')
                                output.write(chunk)
                                remaining -= len(chunk)
                    except Exception:
                        source.unlink(missing_ok=True)
                        raise
                    job = app.jobs.submit('upload', source, name,
                                          path=target, overwrite=arg('overwrite').lower() == 'true')
                    return self.reply({'ok': True, 'job': job}, 202)
                if method == 'GET' and path == '/api/files/download':
                    target = validate_path(arg('path'))
                    with app.bound_transport(arg('expected_device')) as peer:
                        metadata = next((entry for entry in peer.list(posixpath.dirname(target))
                                         if entry['path'] == target and entry['type'] == 'file'), None)
                        if not metadata:
                            raise ApiError(404, '文件不存在')
                        self.send_response(200)
                        self.send_header('Content-Type', 'application/octet-stream')
                        self.send_header('Content-Length', str(metadata['size']))
                        self.send_header('Content-Disposition', "attachment; filename*=UTF-8''" + urllib.parse.quote(metadata['name'], safe=''))
                        self.end_headers()
                        peer.download(target, self.wfile.write)
                    return
                if method == 'POST' and path in ('/api/files/mkdir', '/api/files/rename'):
                    data = self.data()
                    with app.bound_transport(data.get('expected_device')) as peer:
                        if path.endswith('/mkdir'):
                            peer.mkdir(data.get('path', '/'), data.get('name'))
                        else:
                            peer.rename(data.get('path'), data.get('name'))
                    return self.reply({'ok': True})
                if method == 'DELETE' and path == '/api/files':
                    with app.bound_transport(arg('expected_device')) as peer:
                        peer.delete(arg('path'))
                    return self.reply({'ok': True})
                if method != 'GET' or path.startswith('/api/'):
                    raise ApiError(404, '找不到这个操作')
                candidate = static_root / ('index.html' if path == '/' else path.lstrip('/'))
                try:
                    candidate.resolve().relative_to(static_root.resolve())
                except ValueError:
                    raise ApiError(403, '路径不可访问')
                if not candidate.is_file():
                    raise ApiError(404, '页面不存在')
                body = candidate.read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', (mimetypes.guess_type(candidate.name)[0] or 'application/octet-stream') + '; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Content-Type-Options', 'nosniff')
                self.end_headers()
                self.wfile.write(body)
            except ApiError as exc:
                self.reply({'ok': False, 'error': exc.message}, exc.status)
            except RemoteError as exc:
                self.reply({'ok': False, 'error': str(exc)}, 409)
            except LibraryError as exc:
                self.reply({'ok': False, 'error': str(exc)}, 409)
            except MusicError as exc:
                self.reply({'ok': False, 'error': str(exc)}, 409)
            except HttpJobError as exc:
                self.reply({'ok': False, 'error': str(exc)}, exc.status)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except Exception as exc:
                self.reply({'ok': False, 'error': '操作失败：' + str(exc)}, 500)
            finally:
                if receiving:
                    with app.selection_lock:
                        app.receiving_count -= 1

    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    app.port = server.server_port
    app.host = host
    return server
