from contextlib import contextmanager
from datetime import datetime, timezone
import ftplib
import hashlib
import io
import json
from pathlib import Path
import posixpath
import re
import threading
import time
import uuid


class RemoteError(Exception):
    pass


def is_dbi_installer(rows):
    """DBI exposes a zero-byte instruction file instead of an SD filesystem."""
    return len(rows) == 1 and any(
        re.fullmatch(r'Place (?:NSP|NSZ|XCI|XCZ|MSP)(?:(?:, | or )(?:NSP|NSZ|XCI|XCZ|MSP))* files here', name)
        and facts.get('type') == 'file' and str(facts.get('size', '0')) == '0'
        for name, facts in rows)


def validate_path(path):
    if not isinstance(path, str) or not path.startswith('/') or '\\' in path:
        raise RemoteError('SD 卡路径不合法')
    if any(ord(c) < 32 for c in path) or '..' in path.split('/'):
        raise RemoteError('SD 卡路径不合法')
    return '/' + '/'.join(p for p in path.split('/') if p and p != '.')


def validate_name(name):
    if not isinstance(name, str) or not name or name in ('.', '..') or len(name) > 220:
        raise RemoteError('文件名不合法')
    if any(c in name for c in '/\\\r\n\x00') or any(ord(c) < 32 for c in name):
        raise RemoteError('文件名不合法')
    return name


class RemoteFTP:
    def __init__(self, host, port=5000, sd_root='/', backup_dir=None, device=None, lock=None):
        self.host, self.port = host, int(port)
        self.sd_root = sd_root.rstrip('/')
        self.backup_dir = Path(backup_dir or 'data/backups')
        self.device = device or {}
        self.lock = lock or threading.RLock()
        self.stage_callback = None

    def remote_path(self, path):
        return self.sd_root + validate_path(path)

    @contextmanager
    def connection(self, file_access=False):
        if file_access and self.device.get('can_browse') is False:
            raise RemoteError('DBI 当前是游戏安装模式。管理文件请切换到「浏览 SD 卡」。')
        with self.lock:
            ftp = ftplib.FTP()
            ftp.encoding = 'utf-8'
            try:
                ftp.connect(self.host, self.port, timeout=12)
                ftp.login('anonymous', 'anonymous@')
                # The same DBI address can change into an installer before the
                # next discovery refresh. Never send ordinary files to it.
                if file_access and is_dbi_installer(self.entries(ftp, '/')):
                    raise RemoteError('DBI 已切换到游戏安装模式。管理文件请切换到「浏览 SD 卡」。')
                yield ftp
            except RemoteError:
                raise
            except (OSError, EOFError, ftplib.Error) as exc:
                raise RemoteError('Switch 连接失败：' + str(exc)) from exc
            finally:
                try:
                    ftp.quit()
                except Exception:
                    ftp.close()

    @staticmethod
    def entries(ftp, path):
        try:
            rows = list(ftp.mlsd(path))
        except ftplib.error_perm as exc:
            if str(exc).startswith('550'):
                raise
            rows = None
        if rows is not None:
            return rows
        lines = []
        ftp.retrlines('LIST ' + path, lines.append)
        result = []
        for line in lines:
            parts = line.split(None, 8)
            if len(parts) == 9 and parts[0][0] in 'd-l':
                result.append((parts[8], {'type': 'dir' if parts[0][0] == 'd' else 'file',
                                          'size': parts[4]}))
        return result

    def list(self, path='/'):
        path = validate_path(path)
        with self.connection(file_access=True) as ftp:
            rows = self.entries(ftp, self.remote_path(path))
        entries = []
        for name, facts in rows:
            if name in ('.', '..') or facts.get('type') in ('cdir', 'pdir'):
                continue
            modified = 0
            try:
                modified = int(datetime.strptime(facts.get('modify', ''), '%Y%m%d%H%M%S').replace(tzinfo=timezone.utc).timestamp())
            except (ValueError, OSError, OverflowError):
                pass
            entries.append({'name': name, 'path': posixpath.join(path, name),
                            'type': 'directory' if facts.get('type') == 'dir' else 'file',
                            'size': int(facts.get('size') or 0), 'modified': modified})
        return sorted(entries, key=lambda e: (e['type'] != 'directory', e['name'].casefold()))

    @staticmethod
    def exists(ftp, path):
        try:
            ftp.voidcmd('TYPE I')
            return ftp.size(path)
        except ftplib.error_perm as exc:
            if str(exc).startswith('550'):
                return None
            raise

    def download(self, path, writer):
        with self.connection(file_access=True) as ftp:
            ftp.retrbinary('RETR ' + self.remote_path(path), writer, blocksize=256 * 1024)

    def upload(self, source, target, overwrite=False, progress=None, cancel=None):
        target = self.remote_path(target)
        temporary = posixpath.join(posixpath.dirname(target), '.switchlink.part-' + uuid.uuid4().hex)
        backup = None
        with self.connection(file_access=True) as ftp:
            old_size = self.exists(ftp, target)
            if old_size is not None and not overwrite:
                raise RemoteError('同名文件已存在，请确认覆盖后重试')
            if old_size is not None:
                self.backup_dir.mkdir(parents=True, exist_ok=True)
                backup = self.backup_dir / (uuid.uuid4().hex + '-' + posixpath.basename(target))
                with backup.open('wb') as output:
                    ftp.retrbinary('RETR ' + target, output.write, blocksize=256 * 1024)
                if backup.stat().st_size != old_size:
                    raise RemoteError('旧文件备份不完整，已停止覆盖')
            sent = 0
            def transferred(data):
                nonlocal sent
                sent += len(data)
                if progress:
                    progress(sent)
                if cancel and cancel.is_set():
                    raise RemoteError('任务已取消，原文件保留')
            try:
                with Path(source).open('rb') as data:
                    ftp.storbinary('STOR ' + temporary, data, blocksize=256 * 1024, callback=transferred)
                if self.exists(ftp, temporary) != Path(source).stat().st_size:
                    raise RemoteError('上传校验失败，原文件保留')
                # Rename the old file first, so a failed promotion does not destroy it.
                previous = target + '.switchlink-backup-' + uuid.uuid4().hex[:8]
                if old_size is not None:
                    ftp.rename(target, previous)
                try:
                    ftp.rename(temporary, target)
                except Exception:
                    if old_size is not None:
                        ftp.rename(previous, target)
                    raise
                if old_size is not None:
                    ftp.delete(previous)
            finally:
                try:
                    ftp.delete(temporary)
                except Exception:
                    pass
        return {'bytes': sent, 'backup': str(backup) if backup else None}

    def mkdir(self, path, name):
        with self.connection(file_access=True) as ftp:
            ftp.mkd(self.remote_path(posixpath.join(validate_path(path), validate_name(name))))

    def rename(self, path, name):
        path = validate_path(path)
        if path == '/':
            raise RemoteError('不能重命名 SD 卡根目录')
        new_path = posixpath.join(posixpath.dirname(path), validate_name(name))
        with self.connection(file_access=True) as ftp:
            parent_names = {n for n, _ in self.entries(ftp, self.remote_path(posixpath.dirname(path)))}
            if name in parent_names:
                raise RemoteError('目标名称已存在')
            ftp.rename(self.remote_path(path), self.remote_path(new_path))

    def delete(self, path):
        path = validate_path(path)
        if path == '/':
            raise RemoteError('不能删除 SD 卡根目录')
        with self.connection(file_access=True) as ftp:
            if self.exists(ftp, self.remote_path(path)) is not None:
                ftp.delete(self.remote_path(path))
            else:
                ftp.rmd(self.remote_path(path))  # Empty folders only, never recursive.

    def install(self, source, name, progress=None, cancel=None):
        if not self.device.get('can_install'):
            raise RemoteError('当前接收端只支持文件管理，请打开 Switch Link 客户端')
        destination = self.device.get('install_path', '/install').rstrip('/') + '/' + validate_name(name)
        with self.connection() as ftp:
            if self.device.get('mode') == 'dbi-install' and not is_dbi_installer(self.entries(ftp, '/')):
                raise RemoteError('DBI 模式已切换，请选择「安装游戏（SD 卡）」并刷新连接后重试。')
            count = 0
            def transferred(data):
                nonlocal count
                count += len(data)
                if progress:
                    progress(count)
                # Never abort console content registration mid-operation.
            with Path(source).open('rb') as data:
                ftp.storbinary('STOR ' + destination, data, blocksize=256 * 1024, callback=transferred)
            if self.stage_callback:
                self.stage_callback('installing')
            if not self.device.get('receipt_supported'):
                return None
            # The native FTP loop may be busy committing the installed title
            # after sending 226. Its normal 12s command timeout is too short.
            ftp.timeout = 60
            if ftp.sock:
                ftp.sock.settimeout(60)
            receipt_path = self.remote_path(self.device.get('receipt_path', '/config/switch-link/last-install.json'))
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                payload = io.BytesIO()
                try:
                    def receipt_chunk(chunk):
                        if payload.tell() + len(chunk) > 65536:
                            raise ValueError('安装回执过大')
                        payload.write(chunk)
                    ftp.retrbinary('RETR ' + receipt_path, receipt_chunk)
                    receipt = json.loads(payload.getvalue())
                    if receipt.get('filename') == name and isinstance(receipt.get('success'), bool):
                        return receipt
                except (ftplib.error_perm, ValueError):
                    pass
                except (OSError, EOFError, ftplib.error_temp, ftplib.error_proto):
                    # Transfer already completed. Losing the result channel
                    # leaves the installation unconfirmed, not falsely failed.
                    ftp.close()
                    return None
                time.sleep(.5)
            return None


def probe_switch(host, port=5000, timeout=1, advertisement=None):
    ftp = ftplib.FTP()
    ftp.encoding = 'utf-8'
    try:
        ftp.connect(host, port, timeout=timeout)
        ftp.login('anonymous', 'anonymous@')
        rows = RemoteFTP.entries(ftp, '/')
        root = {name for name, _ in rows}
        dbi_install = is_dbi_installer(rows)
        sd_root = '/'
        if not dbi_install and not {'switch', 'atmosphere', 'bootloader'}.issubset(root):
            for possible in ('sdmc:', 'sdmc', 'sdcard'):
                if possible in root:
                    nested = {name for name, _ in RemoteFTP.entries(ftp, '/' + possible)}
                    if {'switch', 'atmosphere', 'bootloader'}.issubset(nested):
                        sd_root = '/' + possible
                        break
            else:
                return None
        ad = advertisement or {}
        own_client = not dbi_install and ad.get('type') == 'switch-link' and ad.get('version') == 1
        install_path = '/' if dbi_install else ad.get('install_path', '/install')
        if not isinstance(install_path, str):
            return None
        own_client = own_client and (install_path.strip('/') in root or (
            ad.get('http_install_supported') is True and sd_root == ad.get('sd_root')))
        can_install = dbi_install or (own_client and ad.get('install_ready') is True and install_path.strip('/') in root)
        return {'id': str((ad.get('device_id') if own_client else None) or 'ftp-' + host), 'name': str((ad.get('name') if own_client else None) or '我的 Switch')[:80],
                'ip': host, 'port': int(port), 'connected': True, 'mode': 'switch-link' if own_client else 'dbi-install' if dbi_install else 'dbi',
                'sd_root': sd_root, 'can_browse': not dbi_install, 'can_install': can_install, 'install_path': install_path,
                'receipt_supported': bool(own_client and can_install and ad.get('receipt_supported')),
                'receipt_path': '/config/switch-link/last-install.json',
                'progress_port': ad.get('progress_port') if own_client else None,
                'progress_ready': bool(ad.get('progress_ready')) if own_client else False,
                'current_task': ad.get('task') if own_client else None,
                'http_install_supported': bool(own_client and ad.get('http_install_supported')),
                'app_version': str(ad.get('app_version', '')) if own_client else '',
                'http_install_ready': bool(own_client and ad.get('http_install_ready')),
                'http_formats': list(ad.get('http_formats', [])) if own_client else [],
                'command_port': ad.get('command_port', 28772) if own_client else None,
                'install_reason': '' if can_install else '请在 DBI 选择「启动 FTP 服务器」→「安装游戏（SD 卡）」并保持接收页，网页会自动识别；也可以打开 Switch Link 客户端。',
                'free_bytes': ad.get('free_bytes'), 'total_bytes': ad.get('total_bytes')}
    except (OSError, EOFError, ftplib.Error):
        return None
    finally:
        try:
            ftp.quit()
        except Exception:
            ftp.close()
