from collections import OrderedDict
from pathlib import Path
import queue
import threading
import time
import uuid

from .transport import RemoteError
from .progress import TaskPublisher


class JobManager:
    def __init__(self, transport_provider, data_dir):
        self.transport_provider = transport_provider
        self.data_dir = Path(data_dir)
        self.lock = threading.RLock()
        self.jobs = OrderedDict()
        self.sources = {}
        self.cancel_events = {}
        self.queue = queue.Queue()
        self.worker = threading.Thread(target=self._work, daemon=True)
        self.worker.start()

    def submit(self, kind, source, name, path=None, overwrite=False):
        job_id = uuid.uuid4().hex
        job = {'id': job_id, 'name': name, 'kind': kind, 'state': 'queued',
               'bytes_done': 0, 'bytes_total': Path(source).stat().st_size,
               'progress': 0, 'speed': 0, 'stage': 'queued', 'eta_seconds': None,
               'error': '', 'created_at': time.time(), 'updated_at': time.time()}
        with self.lock:
            self.jobs[job_id] = job
            self.sources[job_id] = (Path(source), path, overwrite)
            self.cancel_events[job_id] = threading.Event()
        self.queue.put(job_id)
        return dict(job)

    def get(self, job_id):
        with self.lock:
            return dict(self.jobs[job_id])

    def snapshot(self):
        with self.lock:
            return [dict(j) for j in reversed(self.jobs.values())]

    def active(self):
        return any(j['state'] in ('queued', 'transferring') for j in self.snapshot())

    def cancel(self, job_id):
        with self.lock:
            job = self.jobs.get(job_id)
            if not job:
                raise RemoteError('找不到任务')
            if job['state'] not in ('queued', 'transferring'):
                raise RemoteError('任务已经结束')
            if job['kind'] == 'install' and job['state'] == 'transferring':
                raise RemoteError('正在安装，请等待本次安装结束后再取消后续任务')
            self.cancel_events[job_id].set()
            if job['state'] == 'queued':
                job['state'] = 'cancelled'

    def _cleanup(self, job_id):
        with self.lock:
            source = self.sources.pop(job_id, (None, None, None))[0]
            self.cancel_events.pop(job_id, None)
        if source is not None:
            try:
                source.resolve().relative_to((self.data_dir / 'inbox').resolve())
            except ValueError:
                return  # Never delete the caller's original source file.
            try:
                source.unlink(missing_ok=True)
            except OSError as exc:
                with self.lock:
                    self.jobs[job_id]['cleanup_error'] = str(exc)

    def _work(self):
        while True:
            job_id = self.queue.get()
            if job_id is None:
                return
            with self.lock:
                job = self.jobs[job_id]
                if job['state'] == 'cancelled':
                    self._cleanup(job_id)
                    continue
                source, path, overwrite = self.sources[job_id]
                job['state'] = 'transferring'
                job['stage'] = 'transferring'
            started = time.monotonic()
            publisher = None
            def progress(sent):
                with self.lock:
                    job['bytes_done'] = sent
                    job['progress'] = round(min(100, sent * 100 / max(1, job['bytes_total'])), 1)
                    job['speed'] = int(sent / max(.05, time.monotonic() - started))
                    job['updated_at'] = time.time()
                    job['eta_seconds'] = max(0, (job['bytes_total'] - sent) / job['speed']) if job['speed'] > 0 else None
                    snapshot = dict(job)
                if publisher:
                    publisher.send(snapshot, 'transferring')
            def stage_changed(stage):
                with self.lock:
                    job['stage'] = stage
                    job['eta_seconds'] = None
                    snapshot = dict(job)
                if publisher:
                    publisher.send(snapshot, stage, force=True)
            try:
                peer = self.transport_provider()
                with self.lock:
                    job['receiver_mode'] = getattr(peer, 'device', {}).get('mode')
                publisher = TaskPublisher(getattr(peer, 'device', {}))
                peer.stage_callback = stage_changed
                publisher.send(dict(job), 'transferring', force=True)
                if job['kind'] == 'upload':
                    peer.upload(source, path, overwrite=overwrite, progress=progress,
                                cancel=self.cancel_events[job_id])
                    state = 'completed'
                else:
                    receipt = peer.install(source, job_id + '_' + job['name'], progress=progress,
                                           cancel=self.cancel_events[job_id])
                    if receipt is None:
                        state = 'awaiting_confirmation'
                    elif receipt.get('success'):
                        state = 'completed'
                    else:
                        raise RemoteError(receipt.get('message') or 'Switch 安装失败，结果码：' + str(receipt.get('result')))
                with self.lock:
                    job['state'] = state
                    job['stage'] = state
                    job['progress'] = 100
                    job['bytes_done'] = job['bytes_total']
                    job['updated_at'] = time.time()
                publisher.send(dict(job), 'completed' if state == 'completed' else 'registering', force=True)
            except Exception as exc:
                with self.lock:
                    job['state'] = 'cancelled' if self.cancel_events[job_id].is_set() else 'failed'
                    job['error'] = str(exc)
                    job['stage'] = job['state']
                if publisher:
                    publisher.send(dict(job), job['state'], force=True)
            finally:
                if publisher:
                    publisher.close()
                self._cleanup(job_id)

    def close(self):
        self.queue.put(None)
        self.worker.join(timeout=3)
