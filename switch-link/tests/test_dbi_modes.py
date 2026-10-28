from pathlib import Path
import unittest
from unittest.mock import patch

import test_transport
from switch_link.discovery import Discovery
from switch_link.transport import RemoteError, RemoteFTP, probe_switch


INSTALL_MARKER = 'Place NSP, NSZ, XCI, XCZ or MSP files here'


class DBIModeTests(unittest.TestCase):
    setUp = test_transport.FTPTests.setUp
    tearDown = test_transport.FTPTests.tearDown

    def install_root(self):
        for name in ('switch', 'atmosphere', 'bootloader'):
            (self.root / name).rmdir()
        (self.root / INSTALL_MARKER).touch()

    def test_install_mode_is_discovered_on_the_same_port_without_sd_directories(self):
        self.install_root()
        device = probe_switch('127.0.0.1', self.port)
        self.assertIsNotNone(device, 'DBI installation root must be recognized')
        self.assertEqual(device['mode'], 'dbi-install')
        self.assertTrue(device['can_install'])
        self.assertFalse(device['can_browse'])
        self.assertEqual(device['install_path'], '/')
        self.assertFalse(device['receipt_supported'])

    def test_mode_changes_keep_the_same_console_identity_and_refresh_capabilities(self):
        first = probe_switch('127.0.0.1', self.port)
        self.install_root()
        second = probe_switch('127.0.0.1', self.port)
        self.assertIsNotNone(second)
        self.assertEqual(first['id'], second['id'])
        self.assertTrue(first['can_browse'])
        self.assertFalse(first['can_install'])
        self.assertTrue(second['can_install'])

    def test_http_native_client_without_ftp_install_root_keeps_version_and_capabilities(self):
        nested=self.root/'sdcard'
        nested.mkdir()
        for name in ('switch','atmosphere','bootloader'):
            (self.root/name).rename(nested/name)
        beacon={'type':'switch-link','version':1,'device_id':'a'*32,'app_version':'0.2.1',
                'sd_root':'/sdcard','install_path':'/install:', 'install_ready':False,
                'http_install_supported':True,'http_install_ready':True,
                'command_port':28772,'http_formats':['nsp','xci']}
        device=probe_switch('127.0.0.1',self.port,advertisement=beacon)
        self.assertEqual(device['mode'],'switch-link')
        self.assertEqual(device['app_version'],'0.2.1')
        self.assertTrue(device['http_install_supported'])
        self.assertTrue(device['http_install_ready'])
        self.assertFalse(device['can_install'],'FTP install remains disabled')

    def test_an_old_native_beacon_cannot_mislabel_dbi_or_enable_native_receipts(self):
        beacon = {'type': 'switch-link', 'version': 1, 'device_id': 'old-native',
                  'name': 'Switch Link', 'install_path': '/install:',
                  'install_ready': True, 'receipt_supported': True}
        device = probe_switch('127.0.0.1', self.port, advertisement=beacon)
        self.assertEqual(device['mode'], 'dbi')
        self.assertEqual(device['id'], 'ftp-127.0.0.1')
        self.install_root()
        device = probe_switch('127.0.0.1', self.port, advertisement=beacon)
        self.assertEqual(device['mode'], 'dbi-install')
        self.assertFalse(device['receipt_supported'])

    def test_installer_streams_the_package_into_root_and_does_not_invent_success(self):
        self.install_root()
        device = probe_switch('127.0.0.1', self.port)
        self.assertIsNotNone(device)
        source = Path(self.tmp.name) / 'owned.nsp'
        source.write_bytes(b'local test fixture, not a real game')
        peer = RemoteFTP('127.0.0.1', self.port, device=device)
        sent = []
        self.assertIsNone(peer.install(source, 'job_owned.nsp', progress=sent.append))
        self.assertEqual((self.root / 'job_owned.nsp').read_bytes(), source.read_bytes())
        self.assertEqual(sent[-1], source.stat().st_size)
        self.assertFalse((self.root / 'install').exists())

    def test_install_mode_rejects_all_file_operations_before_connecting(self):
        device = {'mode': 'dbi-install', 'can_install': True, 'can_browse': False}
        peer = RemoteFTP('127.0.0.1', 1, device=device)
        source = Path(self.tmp.name) / 'source.txt'
        source.write_bytes(b'test')
        calls = [lambda: peer.list('/'), lambda: peer.download('/x', lambda _: None),
                 lambda: peer.upload(source, '/x'), lambda: peer.mkdir('/', 'x'),
                 lambda: peer.rename('/x', 'y'), lambda: peer.delete('/x')]
        for call in calls:
            with self.subTest(call=call), self.assertRaisesRegex(RemoteError, '安装模式'):
                call()

    def test_stale_file_mode_cannot_accidentally_upload_to_the_installer(self):
        self.install_root()
        source = Path(self.tmp.name) / 'source.nsp'
        source.write_bytes(b'fixture')
        peer = RemoteFTP('127.0.0.1', self.port, device={'mode': 'dbi', 'can_browse': True})
        with self.assertRaisesRegex(RemoteError, '安装模式'):
            peer.upload(source, '/must-not-install.nsp')
        self.assertEqual([p.name for p in self.root.iterdir()], [INSTALL_MARKER])

    def test_stale_installer_cannot_copy_a_package_to_an_ordinary_sd_directory(self):
        source = Path(self.tmp.name) / 'source.nsp'
        source.write_bytes(b'fixture')
        peer = RemoteFTP('127.0.0.1', self.port, device={
            'mode': 'dbi-install', 'can_browse': False, 'can_install': True, 'install_path': '/'})
        with self.assertRaisesRegex(RemoteError, '模式'):
            peer.install(source, 'must-not-copy.nsp')
        self.assertFalse((self.root / 'must-not-copy.nsp').exists())


class DiscoveryModeTests(unittest.TestCase):
    def test_refresh_falls_back_to_the_legacy_installer_port_on_the_known_console(self):
        discovery = Discovery(auto=False)
        device = {'id': 'ftp-192.0.2.75', 'ip': '192.0.2.75', 'port': 5000}
        installer = dict(device, port=6000, can_install=True, can_browse=False, connected=True)
        def probe(host, port=5000, **kwargs):
            self.assertEqual(host, device['ip'])
            return dict(installer) if port == 6000 else None
        with patch('switch_link.discovery.probe_switch', side_effect=probe) as mock:
            current = discovery.refresh(device)
        self.assertIsNotNone(current)
        self.assertEqual(current['port'], 6000)
        self.assertEqual([call.args[1] for call in mock.call_args_list], [5000, 6000])
