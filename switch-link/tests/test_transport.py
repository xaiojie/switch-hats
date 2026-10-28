from pathlib import Path
import logging
import os
import json
import time
import tempfile
import threading
import unittest

from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

from switch_link.transport import RemoteFTP, RemoteError, probe_switch, validate_path


class FTPTests(unittest.TestCase):
    def setUp(self):
        logging.getLogger('pyftpdlib').setLevel(logging.CRITICAL)
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / 'card'
        self.root.mkdir()
        for name in ('switch', 'atmosphere', 'bootloader'):
            (self.root / name).mkdir()
        authorizer = DummyAuthorizer()
        authorizer.add_anonymous(str(self.root), perm='elradfmwMT')
        handler = type('TestFTPHandler', (FTPHandler,), {'authorizer': authorizer})
        self.handler_class = handler
        self.server = FTPServer(('127.0.0.1', 0), handler)
        self.port = self.server.socket.getsockname()[1]
        ready = threading.Event()
        self.server.ioloop.call_later(0, ready.set)
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       kwargs={'timeout': .05, 'handle_exit': False}, daemon=True)
        self.thread.start()
        self.assertTrue(ready.wait(3), 'Local FTP event loop did not start')
        self.remote = RemoteFTP('127.0.0.1', self.port, backup_dir=Path(self.tmp.name) / 'backups')

    def tearDown(self):
        self.server.close_all()
        self.thread.join(timeout=2)
        self.tmp.cleanup()

    def test_traversal_and_root_mutation_are_rejected(self):
        for path in ('../keys', '/switch/../boot.dat', '/a\x00b', 'relative'):
            with self.subTest(path=path), self.assertRaises(RemoteError):
                validate_path(path)
        with self.assertRaises(RemoteError):
            self.remote.delete('/')
        self.assertTrue((self.root / 'switch').is_dir())

    def test_probe_requires_switch_markers_and_does_not_assume_install_folder_is_an_installer(self):
        (self.root / 'install').mkdir()
        device = probe_switch('127.0.0.1', self.port)
        self.assertTrue(device)
        self.assertFalse(device['can_install'])
        (self.root / 'atmosphere').rmdir()
        self.assertIsNone(probe_switch('127.0.0.1', self.port))

    def test_console_can_explicitly_disable_installation(self):
        (self.root / 'install').mkdir()
        beacon = {'type': 'switch-link', 'version': 1, 'device_id': 'test-device',
                  'install_path': '/install', 'install_ready': False}
        device = probe_switch('127.0.0.1', self.port, advertisement=beacon)
        self.assertFalse(device['can_install'])

    def test_listing_handles_unicode_and_browsing_subdirectories(self):
        (self.root / 'switch' / '中文.nro').write_bytes(b'hello')
        entries = self.remote.list('/switch')
        self.assertEqual([(x['name'], x['size'], x['type']) for x in entries], [('中文.nro', 5, 'file')])
        self.assertEqual(entries[0]['path'], '/switch/中文.nro')

    def test_upload_does_not_overwrite_without_permission_and_backs_up_replaced_file(self):
        target = self.root / 'switch' / 'tool.nro'
        target.write_bytes(b'old')
        source = Path(self.tmp.name) / 'source'
        source.write_bytes(b'new content')
        with self.assertRaises(RemoteError):
            self.remote.upload(source, '/switch/tool.nro')
        self.assertEqual(target.read_bytes(), b'old')
        result = self.remote.upload(source, '/switch/tool.nro', overwrite=True)
        self.assertEqual(target.read_bytes(), b'new content')
        self.assertEqual(Path(result['backup']).read_bytes(), b'old')
        self.assertEqual(result['bytes'], 11)
        self.assertFalse(any('.part-' in p.name for p in (self.root / 'switch').iterdir()))

    def test_rename_collision_is_rejected_and_explicit_delete_works(self):
        folder = self.root / 'switch'
        (folder / 'a.txt').write_bytes(b'a')
        (folder / 'b.txt').write_bytes(b'b')
        with self.assertRaises(RemoteError):
            self.remote.rename('/switch/a.txt', 'b.txt')
        self.remote.rename('/switch/a.txt', 'c.txt')
        self.assertEqual((folder / 'c.txt').read_bytes(), b'a')
        self.remote.delete('/switch/c.txt')
        self.assertTrue((folder / 'b.txt').exists())

    def test_missing_directory_fails_instead_of_reporting_an_empty_card(self):
        with self.assertRaises(RemoteError):
            self.remote.list('/does-not-exist')

    def test_ftp_utc_epoch_and_real_dates_do_not_depend_on_windows_local_timezone(self):
        for name, timestamp in (('epoch.txt', 0), ('recent.txt', 1700000000)):
            item = self.root / 'switch' / name
            item.write_bytes(b'x')
            os.utime(item, (timestamp, timestamp))
        entries = {entry['name']: entry for entry in self.remote.list('/switch')}
        self.assertEqual(entries['epoch.txt']['modified'], 0)
        self.assertEqual(entries['recent.txt']['modified'], 1700000000)

    def test_slow_native_finalization_still_yields_a_matching_install_receipt(self):
        (self.root / 'install').mkdir()
        receipt_dir = self.root / 'config' / 'switch-link'
        receipt_dir.mkdir(parents=True)
        (receipt_dir / 'last-install.json').write_text(json.dumps({'filename': 'job_fixture.nsp', 'success': True, 'result': 0}))
        source = Path(self.tmp.name) / 'own.nsp'
        source.write_bytes(b'test owned source')
        original_retr = self.handler_class.ftp_RETR
        def delayed_receipt(handler, file):
            if str(file).endswith('last-install.json'):
                time.sleep(13)
            return original_retr(handler, file)
        self.handler_class.ftp_RETR = delayed_receipt
        peer = RemoteFTP('127.0.0.1', self.port, device={'can_install': True, 'install_path': '/install', 'receipt_supported': True})
        receipt = peer.install(source, 'job_fixture.nsp')
        self.assertTrue(receipt['success'])


if __name__ == '__main__':
    unittest.main()
