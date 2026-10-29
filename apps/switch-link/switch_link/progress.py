import json
import socket
import time


class TaskPublisher:
    """Small, throttled display-only telemetry to the already selected console."""
    def __init__(self, device):
        self.device = device
        self.socket = None
        self.last_sent = 0

    def send(self, job, stage, force=False):
        port = self.device.get('progress_port')
        if self.device.get('mode') != 'switch-link' or not isinstance(port, int) or not 1 <= port <= 65535:
            return False
        now = time.monotonic()
        if not force and now - self.last_sent < .5:
            return False
        name = str(job.get('name', ''))[:220]
        message = {'type': 'switch-link-task', 'version': 1, 'device_id': self.device['id'],
                   'id': job['id'], 'name': name, 'kind': job.get('kind', 'upload'), 'stage': stage,
                   'wire_name': job['id'] + '_' + name if job.get('kind') == 'install' else name,
                   'bytes_done': job.get('bytes_done', 0), 'bytes_total': job.get('bytes_total', 0),
                   'speed': job.get('speed', 0), 'progress': job.get('progress', 0)}
        try:
            if self.socket is None:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.socket.setblocking(False)
            self.socket.sendto(json.dumps(message, ensure_ascii=False).encode('utf-8'),
                               (self.device['ip'], port))
            self.last_sent = now
            return True
        except OSError:
            return False  # Missing telemetry never interrupts the actual file transfer.

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None
