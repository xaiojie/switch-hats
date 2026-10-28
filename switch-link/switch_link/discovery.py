from concurrent.futures import ThreadPoolExecutor, as_completed
import ipaddress
import json
import math
import re
import socket
import threading
import time

from .transport import probe_switch


def local_addresses():
    addresses = []
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(('8.8.8.8', 80))  # Route lookup only; no packet is sent.
        addresses.append(sock.getsockname()[0])
    except OSError:
        pass
    finally:
        sock.close()
    try:
        addresses.extend(i[4][0] for i in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET))
    except OSError:
        pass
    return list(dict.fromkeys(a for a in addresses if ipaddress.ip_address(a).is_private
                             and not ipaddress.ip_address(a).is_loopback
                             and not ipaddress.ip_address(a).is_link_local))


def decode_beacon(data, source):
    try:
        if len(data) > 4096:
            return None
        ad = json.loads(data)
        if not isinstance(ad, dict) or ad.get('type') != 'switch-link' or ad.get('version') != 1:
            return None
        port = int(ad.get('ftp_port', 5000))
        if not 1 <= port <= 65535:
            return None
        if not isinstance(ad.get('device_id', ''), str) or len(ad.get('device_id', '')) > 100:
            return None
        if 'name' in ad and (not isinstance(ad['name'], str) or len(ad['name']) > 80):
            return None
        if 'app_version' in ad and (not isinstance(ad['app_version'], str) or len(ad['app_version']) > 32):
            return None
        for field in ('install_path', 'sd_root', 'receipt_path'):
            if field in ad and (not isinstance(ad[field], str) or len(ad[field]) > 200
                                or not ad[field].startswith('/') or '..' in ad[field].split('/')
                                or any(ord(c) < 32 for c in ad[field])):
                return None
        for field in ('install_ready', 'receipt_supported', 'progress_ready', 'http_install_supported', 'http_install_ready'):
            if field in ad and not isinstance(ad[field], bool):
                return None
        progress_port = ad.get('progress_port')
        if progress_port is not None and (type(progress_port) is not int or not 1 <= progress_port <= 65535):
            return None
        command_port = ad.get('command_port')
        if command_port is not None and (type(command_port) is not int or not 1 <= command_port <= 65535):
            return None
        formats = ad.get('http_formats', [])
        if not isinstance(formats, list) or len(formats) > 4 or any(f not in ('nsp','xci','nsz','xcz') for f in formats):
            return None
        if ad.get('http_install_supported') is True and (
                not re.fullmatch('[0-9a-f]{32}', ad.get('device_id', '')) or not formats
                or type(command_port) is not int):
            return None
        task = ad.get('task')
        if task is not None:
            valid = isinstance(task, dict) and isinstance(task.get('id'), str) and re.fullmatch('[0-9a-f]{32}', task['id'])
            if valid:
                valid = task.get('stage') in ('transferring', 'installing', 'registering', 'completed', 'failed', 'cancelled')
                for key in ('progress', 'bytes_done', 'bytes_total', 'speed', 'eta_seconds'):
                    value = task.get(key)
                    if value is not None and (type(value) not in (int, float) or not math.isfinite(value) or value < 0
                                              or (key == 'progress' and value > 100)):
                        valid = False
            if not valid:
                ad.pop('task', None)
        ad['ftp_port'] = port
        ad['host'] = source
        return ad
    except (ValueError, TypeError, OverflowError, RecursionError):
        return None


class Discovery:
    def __init__(self, seeds=(), auto=True):
        self.lock = threading.RLock()
        self.devices = {}
        self.ads = {}
        self.seeds = list(seeds)
        self.running = False
        self.error = ''
        self.stop = threading.Event()
        self.listener = None
        self.busy = lambda: False
        self.on_advertisement = lambda ad: None
        if auto:
            self.listener = threading.Thread(target=self._listen, daemon=True)
            self.listener.start()
            self.scan()

    def snapshot(self):
        with self.lock:
            devices = [dict(device) for device in self.devices.values()]
            for device in devices:
                if time.time() - device.get('last_seen', 0) > 30:
                    device['connected'] = False
            return {'devices': devices, 'scanning': self.running, 'error': self.error}

    def get(self, device_id):
        return next((d for d in self.snapshot()['devices'] if d['id'] == device_id), None)

    def refresh(self, device):
        current = self._probe(device['ip'], timeout=2)
        with self.lock:
            if current:
                current['last_seen'] = time.time()
                self.devices[current['id']] = current
                return current
            if device['id'] in self.devices:
                self.devices[device['id']]['connected'] = False
        return None

    def scan(self):
        with self.lock:
            if self.running:
                return
            self.running = True
            self.error = ''
        threading.Thread(target=self._scan, daemon=True).start()

    def _scan(self):
        # Discovery is cooperative: connect only to previously selected consoles
        # or peers advertising our protocol, never sweep unrelated LAN hosts.
        candidates = set(self.seeds) | set(self.ads)
        with self.lock:
            candidates.update(device['ip'] for device in self.devices.values())
        found = []
        started = time.time()
        try:
            with ThreadPoolExecutor(max_workers=24) as pool:
                futures = [pool.submit(self._probe, host) for host in sorted(candidates)[:16]]
                for future in as_completed(futures):
                    result = future.result()
                    if result:
                        found.append(result)
            if not candidates:
                self.stop.wait(2.5)  # The console advertises every two seconds.
            with self.lock:
                for device in self.devices.values():
                    if device.get('last_seen', 0) >= started:
                        found.append(device)
                    else:
                        device['connected'] = False
                for device in found:
                    self.devices[device['id']] = device
                if not found:
                    self.error = '尚未发现 Switch。请保持客户端打开，并让两台设备连接同一局域网。'
        except Exception as exc:
            with self.lock:
                self.error = '设备搜索失败：' + str(exc)
        finally:
            with self.lock:
                self.running = False

    def _probe(self, host, timeout=.45):
        if self.stop.is_set():
            return None
        ad = self.ads.get(host)
        # Recent DBI versions reuse 5000 with a different virtual root; older
        # versions also expose installation on 6000. Probe only known peers.
        ports = dict.fromkeys([ad.get('ftp_port', 5000) if ad else 5000, 5000, 6000])
        for port in ports:
            device = probe_switch(host, port, timeout=timeout, advertisement=ad)
            if device:
                device['last_seen'] = time.time()
                return device
        return None

    def observe(self, ad):
        host = ad['host']
        self.ads[host] = ad
        self.on_advertisement(ad)
        if self.busy():
            with self.lock:
                known = self.devices.get(ad.get('device_id'))
                if known and known.get('ip') == host:
                    known['last_seen'] = time.time()
                    known['connected'] = True
                    known['current_task'] = ad.get('task')
            return
        device = self._probe(host)
        if device:
            with self.lock:
                self.devices[device['id']] = device
                self.error = ''

    def _listen(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1)
        try:
            sock.bind(('', 28771))
            while not self.stop.is_set():
                try:
                    data, peer = sock.recvfrom(4097)
                    ad = decode_beacon(data, peer[0])
                    if ad:
                        self.observe(ad)
                except socket.timeout:
                    pass
                except OSError:
                    break
                except (ValueError, TypeError, AttributeError, OverflowError, RecursionError):
                    # One malformed peer must not stop future discovery.
                    continue
        except OSError:
            # Bounded scan remains usable if UDP binding is unavailable.
            pass
        finally:
            sock.close()

    def close(self):
        self.stop.set()
        if self.listener:
            self.listener.join(timeout=2)
