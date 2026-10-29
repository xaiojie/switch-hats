"""HTTP payload jobs installed by the native Switch Link receiver, never DBI."""
import json
import math
from pathlib import Path
import secrets
import socket
import threading
import time
from urllib.parse import quote, urlsplit
import uuid

from .library import package_name


class HttpJobError(ValueError):
    def __init__(self, message, status=409):
        super().__init__(message)
        self.status = status


class HttpInstallJobs:
    TERMINAL = {'completed', 'failed', 'cancelled'}

    def __init__(self, library, sender=None, auto=True):
        self.library = library
        self.lock = threading.RLock()
        self.records = {}
        self.base_url = None
        self.current = None
        self.sender = sender or self._send
        self.stop = threading.Event()
        self.worker = None
        if auto:
            self.worker = threading.Thread(target=self._work, daemon=True)
            self.worker.start()

    @staticmethod
    def _send(packet, peer):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            # The console pins both HTTP origins to this UDP sender address.
            host = urlsplit(json.loads(packet)['url']).hostname
            sock.bind((host, 0))
            sock.sendto(packet, peer)

    def _source_origin(self, device):
        # A VPN/default Internet interface may differ from the route to Switch.
        # UDP connect is a local route lookup and sends no packet.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect((device['ip'], int(device.get('command_port', 28772))))
            host = sock.getsockname()[0]
        return 'http://%s:%d' % (host, urlsplit(self.base_url).port or 80)

    def configure(self, base_url):
        self.base_url = base_url.rstrip('/')

    def submit(self, device, name):
        if (not device or not device.get('connected') or device.get('mode') != 'switch-link'
                or device.get('http_install_supported') is not True):
            raise HttpJobError('请打开 Switch Link 0.2.3 或更新版接收页面；旧版和 DBI 不支持本工具的 HTTP 安装')
        if not self.base_url:
            raise HttpJobError('主机 HTTP 安装服务尚未就绪')
        package_name(name)
        if '..' in name:
            raise HttpJobError('主机安装不接受文件名中连续的两个点，请先更改安装包文件名')
        if len(name.encode('utf-8')) > 200:
            raise HttpJobError('安装包文件名太长，请缩短到 200 字节以内')
        if Path(name).suffix.lower().lstrip('.') not in device.get('http_formats', []):
            raise HttpJobError('当前 Switch Link 主机版本不支持此安装包格式')
        path = self.library.path(name)
        try:
            stat = path.stat()
        except OSError:
            raise HttpJobError('游戏库中没有这个安装包，请先添加文件', 404)
        if not path.is_file() or not 0 < stat.st_size < 128 * 1024**3:
            raise HttpJobError('安装包不存在、为空或大小超过支持范围')
        with self.lock:
            if (not device.get('http_install_ready')
                    and not any(r['device']['id'] == device['id'] and r['job']['state'] not in self.TERMINAL for r in self.records.values())):
                raise HttpJobError('Switch Link 当前未在空闲接收页面，请结束当前任务后再添加安装')
            job_id, token = uuid.uuid4().hex, secrets.token_hex(32)
            now = time.time()
            job = {'id': job_id, 'name': name, 'kind': 'install', 'transport': 'http',
                   'receiver_mode': 'switch-link-http', 'state': 'queued', 'stage': 'queued',
                   'bytes_done': 0, 'bytes_total': stat.st_size, 'network_bytes': 0,
                   'progress': 0, 'speed': 0, 'eta_seconds': None, 'error': '',
                   'created_at': now, 'updated_at': now}
            self.records[job_id] = {'job': job, 'token': token, 'device': dict(device),
                'path': path, 'stamp': (stat.st_size, stat.st_mtime_ns),
                'sent_at': None, 'last_send': 0, 'ack': False, 'receipt': None}
            return dict(job)

    def snapshot(self):
        with self.lock:
            return [dict(r['job']) for r in reversed(self.records.values())]

    def active(self):
        return any(j['state'] not in self.TERMINAL for j in self.snapshot())

    def cancel(self, job_id):
        with self.lock:
            record = self.records.get(job_id)
            if not record:
                raise HttpJobError('找不到 HTTP 安装任务', 404)
            if record['sent_at'] is not None or record['job']['state'] != 'queued':
                raise HttpJobError('任务已发给主机，不能把正在安装的任务当作普通上传取消；请先在 Switch Link 查看状态')
            record['job'].update(state='cancelled', stage='cancelled', updated_at=time.time())

    def tick(self, force=False):
        with self.lock:
            current = self.records.get(self.current)
            if current and current['job']['state'] in self.TERMINAL:
                self.current = None
            if self.current is None:
                self.current = next((key for key, r in self.records.items() if r['job']['state'] == 'queued'), None)
            record = self.records.get(self.current)
            if not record or record['ack'] or not self.base_url:
                return
            now = time.monotonic()
            if record['sent_at'] is not None and now - record['sent_at'] > 20:
                record['job'].update(state='awaiting_confirmation', stage='awaiting_confirmation',
                    error='主机任务确认尚未收到，请查看 Switch Link；未自动重新安装', updated_at=time.time())
                return
            if not force and now - record['last_send'] < 1:
                return
            job, device = record['job'], record['device']
            try:
                prefix = self._source_origin(device)
            except OSError as exc:
                job['error'] = '无法确定到主机的局域网地址：' + str(exc)
                return
            command = {'type': 'switch-link-install', 'version': 1, 'device_id': device['id'],
                'id': job['id'], 'name': job['name'], 'wire_name': job['id'] + '_' + job['name'],
                'url': prefix + '/link-files/' + job['id'] + '/' + record['token'] + '/' + quote(job['name'], safe=''),
                'result_url': prefix + '/link-result/' + job['id'] + '/' + record['token'], 'size': job['bytes_total']}
            packet = json.dumps(command, ensure_ascii=False, separators=(',', ':')).encode()
            if len(packet) > 4096:
                job.update(state='failed', stage='failed', error='安装任务名称过长', updated_at=time.time())
                return
            if record['sent_at'] is None:
                record['sent_at'] = now
            record['last_send'] = now
            try:
                self.sender(packet, (device['ip'], int(device.get('command_port', 28772))))
                job['stage'] = 'waiting_console'
            except OSError as exc:
                job['error'] = '等待主机接收安装任务：' + str(exc)

    def _authorize(self, job_id, token, peer):
        record = self.records.get(job_id)
        if (not record or not isinstance(token, str) or not secrets.compare_digest(record['token'], token)
                or peer != record['device']['ip']):
            raise HttpJobError('安装任务凭据或主机地址不匹配', 403)
        return record

    def source(self, job_id, token, name, peer):
        with self.lock:
            r = self._authorize(job_id, token, peer)
            if r['job']['name'] != name or r['sent_at'] is None:
                raise HttpJobError('安装包与任务不匹配', 403)
            if r['job']['state'] in self.TERMINAL:
                raise HttpJobError('本次安装任务已结束')
            stat = r['path'].stat()
            if (stat.st_size, stat.st_mtime_ns) != r['stamp']:
                raise HttpJobError('安装包已被修改，请重新确认后创建新任务')
            r['ack'] = True
            if r['job']['state'] == 'queued':
                r['job'].update(state='transferring', stage='transferring', error='', updated_at=time.time())
            return r['path']

    def network(self, job_id, count):
        with self.lock:
            r = self.records.get(job_id)
            if r:
                j = r['job']
                j['network_bytes'] += count
                j['bytes_done'] = min(j['bytes_total'], j['network_bytes'])
                j['speed'] = int(j['network_bytes'] / max(.05, time.monotonic() - r['sent_at']))
                j['updated_at'] = time.time()

    def observe(self, ad):
        task = ad.get('task')
        if not isinstance(task, dict) or task.get('source') != 'console':
            return
        with self.lock:
            r = self.records.get(task.get('id'))
            if (not r or r['device']['id'] != ad.get('device_id') or r['device']['ip'] != ad.get('host')
                    or r['job']['state'] in self.TERMINAL or r['sent_at'] is None):
                return
            r['ack'] = True
            job = r['job']
            stage = task.get('stage')
            job.update(updated_at=time.time(), native_stage=stage, error='')
            if stage in ('completed', 'failed', 'cancelled'):
                job.update(state='awaiting_confirmation', stage='registering')
            elif stage in ('transferring', 'installing', 'registering'):
                job.update(state='installing', stage=stage)
            if stage == 'registering':
                job.update(native_progress=None, native_bytes_total=0, native_bytes_done=0, native_speed=0)
            else:
                for name in ('progress','bytes_done','bytes_total','speed'):
                    value = task.get(name)
                    if type(value) in (int,float) and math.isfinite(value) and value >= 0:
                        job['native_'+name] = value
            if isinstance(task.get('step'),str): job['native_step'] = task['step'][:256]

    def finish(self, job_id, token, peer, payload):
        with self.lock:
            r = self._authorize(job_id, token, peer)
            j = r['job']
            if (not isinstance(payload, dict) or payload.get('id') != job_id
                    or payload.get('device_id') != r['device']['id']
                    or payload.get('filename') != job_id+'_'+j['name']
                    or type(payload.get('success')) is not bool
                    or type(payload.get('registered_titles')) is not int or payload['registered_titles'] < 0
                    or type(payload.get('result')) is not int or not 0 <= payload['result'] < 2**32):
                raise HttpJobError('主机安装回执与本次任务不匹配', 403)
            if r['receipt'] is not None:
                if payload != r['receipt']:
                    raise HttpJobError('本次任务已经有不同的结果回执')
                return dict(j)
            r['receipt'] = dict(payload)
            success = payload['success'] and payload['result'] == 0 and payload['registered_titles'] > 0
            j.update(state='completed' if success else 'failed', stage='completed' if success else 'failed',
                progress=100 if success else j['progress'], result=payload['result'],
                registered_titles=payload['registered_titles'],
                error='' if success else str(payload.get('message') or '主机未确认安装成功')[:512], updated_at=time.time())
            return dict(j)

    def _work(self):
        while not self.stop.wait(.5):
            self.tick()

    def close(self):
        self.stop.set()
        if self.worker: self.worker.join(timeout=2)
