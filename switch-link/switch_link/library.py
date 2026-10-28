"""Computer-side HTTP package library; never treats delivery as installation."""
from collections import deque
import hashlib
import html
import json
import math
from pathlib import Path
import re
import shutil
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, quote, urlsplit
from urllib.request import build_opener, ProxyHandler
import uuid


class LibraryError(Exception):
    pass


def package_name(name):
    if (not isinstance(name, str) or not name or name.startswith('.') or len(name) > 200
            or any(c in name for c in '/\\:<>"|?*') or any(ord(c) < 32 for c in name)
            or name.endswith((' ', '.')) or Path(name).suffix.lower() not in ('.nsp', '.nsz', '.xci', '.xcz')):
        raise LibraryError('请选择名称合法的 NSP、NSZ、XCI 或 XCZ 安装包')
    return name


class Library:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.id = hashlib.sha256(str(self.root).casefold().encode()).hexdigest()[:24]
        self.lock = threading.RLock()
        self.pending = set()

    def path(self, name):
        path = self.root / package_name(name)
        if path.resolve().parent != self.root or path.is_symlink():
            raise LibraryError('安装包路径不可访问')
        return path

    def files(self):
        result = []
        for path in self.root.iterdir():
            try:
                safe = self.path(path.name)
                if safe.is_file():
                    stat = safe.stat()
                    result.append({'name': path.name, 'size': stat.st_size, 'modified': stat.st_mtime,
                                   'state': 'ready', 'href': '/files/' + quote(path.name, safe='')})
            except (LibraryError, OSError):
                continue
        return sorted(result, key=lambda e: e['name'].casefold())

    def receive(self, name, reader, size):
        target = self.path(name)
        if not 0 < size < 128 * 1024**3:
            raise LibraryError('文件大小不合法或文件为空')
        with self.lock:
            if target.exists() or name.casefold() in self.pending:
                raise LibraryError('游戏库已有同名文件，已保留原文件；请先更改新文件名')
            if shutil.disk_usage(self.root).free < size + 128 * 1024**2:
                raise LibraryError('电脑游戏库磁盘空间不足')
            self.pending.add(name.casefold())
        temporary = self.root / ('.switchlink-' + uuid.uuid4().hex + '.part')
        try:
            with temporary.open('xb') as output:
                remaining = size
                while remaining:
                    chunk = reader.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise LibraryError('安装包接收中断，未加入游戏库，请重新上传')
                    output.write(chunk)
                    remaining -= len(chunk)
            with self.lock:
                # Exclusive publication never replaces an existing in-use file.
                try:
                    target.hardlink_to(temporary)
                except FileExistsError:
                    raise LibraryError('同名文件已经存在，已保留原文件')
            return {'name': name, 'size': size, 'state': 'ready', 'href': '/files/' + quote(name, safe='')}
        finally:
            temporary.unlink(missing_ok=True)
            with self.lock:
                self.pending.discard(name.casefold())


class HttpShare:
    def __init__(self, library, host='0.0.0.0', port=8080, advertise='127.0.0.1'):
        self.library, self.host, self.port, self.advertise = library, host, port, advertise
        self.url = 'http://%s:%d' % (advertise, port)
        self.owned = False
        self.server = self.thread = None
        self.external = False
        self.error = ''
        self.link_jobs = None
        self.lock = threading.RLock()
        self.transfers = deque(maxlen=20)
        self.switch_access = None
        self.cache = None
        self.checked = 0
        self.opener = build_opener(ProxyHandler({}))

    def _existing(self):
        try:
            with self.opener.open(self.url + '/api/status', timeout=.8) as response:
                data = json.load(response)
            if (isinstance(data, dict) and data.get('app') in ('Switch Link HTTP', '爬爬的 Switch 游戏库')
                    and Path(data.get('dir', '')).resolve() == self.library.root):
                return data
        except (OSError, ValueError, TypeError):
            pass
        return None

    def start(self, reuse=True):
        if reuse and self.port and self._existing():
            self.external = True
            return
        share = self
        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.0'
            def log_message(self, *args): pass
            def do_HEAD(self): self.serve(head=True)
            def do_GET(self): self.serve()
            def do_POST(self):
                parts = urlsplit(self.path).path.split('/')
                if len(parts) != 4 or parts[1] != 'link-result' or share.link_jobs is None:
                    return self.send_error(405)
                try:
                    size = int(self.headers.get('Content-Length', 0))
                    if not 0 < size <= 65536: raise ValueError('invalid result body')
                    payload = json.loads(self.rfile.read(size))
                    share.link_jobs.finish(parts[2], parts[3], self.client_address[0], payload)
                    return self.body(b'{"ok":true}', 'application/json')
                except (ValueError, TypeError, RecursionError) as exc:
                    return self.send_error(getattr(exc, 'status', 400))
            def do_PUT(self): self.send_error(405)
            def do_DELETE(self): self.send_error(405)
            def body(self, payload, content_type):
                self.send_response(200)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(payload)))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                if self.command != 'HEAD': self.wfile.write(payload)
            def serve(self, head=False):
                path = unquote(urlsplit(self.path).path)
                dbi = bool(re.match(r'DBI(?:/|\b)', self.headers.get('User-Agent', ''), re.I))
                if path == '/api/status':
                    data = share.snapshot()
                    data.update(app='Switch Link HTTP', dir=str(share.library.root), port=share.port)
                    return self.body(json.dumps(data, ensure_ascii=False).encode(), 'application/json; charset=utf-8')
                if path in ('/', '/files', '/files/'):
                    if dbi and not head: share.observe(self.client_address[0], '/files/')
                    links = ['<a href="%s">%s</a>  %d' % (entry['href'], html.escape(entry['name']), entry['size'])
                             for entry in share.library.files()]
                    return self.body(('<!doctype html><meta charset="utf-8"><h1>Index of /files/</h1><pre>'
                                      + '\n'.join(links) + '</pre>').encode(), 'text/html; charset=utf-8')
                scoped_job = None
                if path.startswith('/link-files/') and share.link_jobs:
                    parts = path.split('/',4)
                    if len(parts) != 5: return self.send_error(404)
                    try:
                        target = share.link_jobs.source(parts[2],parts[3],parts[4],self.client_address[0])
                        source = target.open('rb')
                        scoped_job = parts[2]
                    except (ValueError, OSError) as exc:
                        return self.send_error(getattr(exc, 'status', 404))
                elif path.startswith('/files/'):
                    try:
                        target = share.library.path(path[len('/files/'):])
                        source = target.open('rb')
                    except (LibraryError, OSError):
                        return self.send_error(404)
                else:
                    return self.send_error(404)
                with source:
                    size = target.stat().st_size
                    start, end, status = 0, size - 1, 200
                    if self.headers.get('Range'):
                        match = re.fullmatch(r'bytes=(\d*)-(\d*)', self.headers['Range'])
                        if not match or not any(match.groups()):
                            return self.range_error(size)
                        a, b = match.groups()
                        if not a:
                            start = max(0, size - int(b))
                            if int(b) == 0: return self.range_error(size)
                        else:
                            start, end = int(a), min(int(b), size-1) if b else size-1
                        if start > end or start >= size: return self.range_error(size)
                        status = 206
                    length = max(0, end - start + 1)
                    self.send_response(status)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(length))
                    self.send_header('Accept-Ranges', 'bytes')
                    if status == 206: self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
                    self.end_headers()
                    if head: return
                    transfer = None
                    if dbi or scoped_job:
                        share.observe(self.client_address[0], '/files/' + quote(target.name, safe=''))
                        transfer = {'id': uuid.uuid4().hex, 'name': target.name, 'ip': self.client_address[0],
                                    'state': 'transferring', 'bytes_done': 0, 'bytes_total': length,
                                    'file_size': size, 'range_start': start, 'speed': 0, 'created_at': time.time()}
                        with share.lock: share.transfers.appendleft(transfer)
                    began = time.monotonic()
                    try:
                        source.seek(start)
                        remaining = length
                        while remaining:
                            chunk = source.read(min(1024 * 1024, remaining))
                            if not chunk: raise OSError('source ended early')
                            self.wfile.write(chunk)
                            remaining -= len(chunk)
                            if scoped_job: share.link_jobs.network(scoped_job, len(chunk))
                            if transfer is not None:
                                with share.lock:
                                    transfer.update(bytes_done=length-remaining, speed=int((length-remaining)/max(.05,time.monotonic()-began)))
                        if transfer is not None:
                            with share.lock: transfer['state'] = 'sent'
                    except OSError:
                        if transfer is not None:
                            with share.lock: transfer['state'] = 'interrupted'
            def range_error(self, size):
                self.send_response(416)
                self.send_header('Content-Range', 'bytes */%d' % size)
                self.send_header('Content-Length', '0')
                self.end_headers()

        class Server(ThreadingHTTPServer):
            daemon_threads = True
            allow_reuse_address = False
            def server_bind(self):
                if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
                    self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                super().server_bind()
        try:
            self.server = Server((self.host, self.port), Handler)
            self.port = self.server.server_port
            self.url = 'http://%s:%d' % (self.advertise, self.port)
            self.owned = True
            self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
            self.thread.start()
        except OSError:
            self.error = 'HTTP 端口被其他服务使用，未替换或停止该服务；请检查游戏库设置'

    def observe(self, ip, path):
        with self.lock:
            self.switch_access = {'ip': ip, 'path': path, 'last_seen': time.time(), 'status': 200}

    def snapshot(self):
        if self.external:
            with self.lock:
                if self.cache is not None and time.monotonic() - self.checked < 2:
                    return dict(self.cache)
                data = self._existing()
                access = (data or {}).get('switch_access') or (data or {}).get('switch_connection', {}).get('http')
                if (not isinstance(access, dict) or not isinstance(access.get('ip'), str)
                        or type(access.get('last_seen')) not in (int, float)
                        or not math.isfinite(access['last_seen']) or access['last_seen'] <= 0):
                    access = None
                result = self.base_status(bool(data))
                result.update(source='existing', progress_available='transfers' in (data or {}),
                              transfers=(data or {}).get('transfers', []), switch_access=access,
                              error='' if data else 'HTTP 游戏库暂不可访问，请检查游戏库服务')
                self.cache, self.checked = result, time.monotonic()
                return dict(result)
        with self.lock:
            result = self.base_status(self.owned)
            result.update(source='managed', progress_available=True,
                          transfers=[dict(item) for item in self.transfers],
                          switch_access=dict(self.switch_access) if self.switch_access else None, error=self.error)
            return result

    def base_status(self, available):
        return {'available': available, 'protocol': 'http', 'library_id': self.library.id,
                'url': self.url + '/files/', 'config': 'SwitchLibrary=ApacheHTTP|' + self.url + '/files/',
                'can_browse_sd': False, 'switch_access': None, 'transfers': []}

    def close(self):
        if self.owned:
            self.server.shutdown()
            self.server.server_close()
            self.thread.join(timeout=3)
            self.owned = False
