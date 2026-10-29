import http.client
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest

from switch_link.http import Application, make_server
from switch_link.discovery import decode_beacon
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer


class ApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.app = Application(Path(self.tmp.name), discover=False)
        self.server = make_server(self.app, '127.0.0.1', 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.app.close()
        self.tmp.cleanup()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            conn.request(method, path, body=body, headers=headers or {})
            response = conn.getresponse()
            payload = response.read()
            return response.status, json.loads(payload)
        finally:
            conn.close()

    def test_status_does_not_invent_connected_console(self):
        code, payload = self.request('GET', '/api/status')
        self.assertEqual(code, 200)
        self.assertIsNone(payload['device'])
        self.assertEqual(payload['jobs'], [])

    def test_live_transfer_keeps_device_reachable_without_a_second_ftp_probe(self):
        self.app.discovery.devices['test'] = {'id': 'test', 'connected': True, 'last_seen': time.time() - 90}
        self.app.selected_id = 'test'
        self.app.jobs.jobs['in-progress'] = {'id': 'in-progress', 'state': 'transferring',
                                             'bytes_done': 100, 'updated_at': time.time()}
        self.assertTrue(self.app.status()['device']['connected'])
        self.app.jobs.jobs['in-progress']['bytes_done'] = 0
        self.assertFalse(self.app.status()['device']['connected'])

    def test_no_device_means_no_upload_or_game_install(self):
        for path in ('/api/files/upload?name=own.nro&path=/switch',):
            code, payload = self.request('PUT', path, b'abc')
            self.assertEqual(code, 409)
            self.assertFalse(payload['ok'])

    def test_dbi_installer_rejects_ordinary_uploads_before_creating_a_job_or_spool(self):
        self.app.selected_id = 'dbi'
        self.app.discovery.devices['dbi'] = {'id': 'dbi', 'connected': True,
            'last_seen': time.time(), 'can_install': True, 'can_browse': False,
            'mode': 'dbi-install', 'ip': '127.0.0.1', 'port': 1}
        code, payload = self.request('PUT', '/api/files/upload?name=owned.nsp&expected_device=dbi', b'abc')
        self.assertEqual(code, 409)
        self.assertIn('安装模式', payload['error'])
        self.assertEqual(self.app.jobs.snapshot(), [])
        self.assertFalse((Path(self.tmp.name) / 'inbox').exists())

    def test_unknown_device_cannot_be_connected_by_arbitrary_host_in_request(self):
        code, _ = self.request('POST', '/api/connect', json.dumps({'id': '192.168.1.1'}),
                               {'Content-Type': 'application/json'})
        self.assertEqual(code, 404)

    def test_device_cannot_change_while_browser_is_still_uploading(self):
        self.app.discovery.devices['test'] = {'id': 'test', 'ip': '127.0.0.1', 'port': 1,
                                              'connected': True, 'last_seen': time.time()}
        self.app.selected_id = 'test'
        upload = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            upload.putrequest('PUT', '/api/files/upload?path=/switch&name=test.txt&expected_device=test')
            upload.putheader('Content-Length', '10')
            upload.endheaders()
            upload.send(b'a')
            time.sleep(.05)
            code, _ = self.request('POST', '/api/connect', '{"id":"other"}')
            self.assertEqual(code, 409)
        finally:
            upload.close()

    def test_cross_origin_mutating_request_is_rejected(self):
        code, _ = self.request('POST', '/api/discover', '{}', {'Origin': 'https://untrusted.example'})
        self.assertEqual(code, 403)

    def test_serves_actual_html_and_javascript_with_the_right_content_types(self):
        static = Path(self.tmp.name) / 'static'
        static.mkdir()
        (static / 'index.html').write_text('<!doctype html><title>Test</title>', encoding='utf-8')
        (static / 'app.js').write_text('console.log("ok")', encoding='utf-8')
        another = make_server(self.app, '127.0.0.1', 0, static_dir=static)
        thread = threading.Thread(target=another.serve_forever, daemon=True)
        thread.start()
        try:
            for path, content_type in (('/', 'text/html'), ('/app.js', 'javascript')):
                conn = http.client.HTTPConnection('127.0.0.1', another.server_port, timeout=3)
                try:
                    conn.request('GET', path)
                    response = conn.getresponse()
                    data = response.read()
                    self.assertEqual(response.status, 200, data)
                    self.assertIn(content_type, response.getheader('Content-Type'))
                finally:
                    conn.close()
        finally:
            another.shutdown()
            another.server_close()
            thread.join()

    def test_malformed_beacons_are_ignored_and_udp_source_address_wins(self):
        for payload in (b'broken', b'[]', b'{"type":"other"}', b'{"type":"switch-link","version":1,"ftp_port":99999}',
                        b'{"type":"switch-link","version":1,"device_id":"local","install_path":[]}',
                        b'{"type":"switch-link","version":1,"device_id":"local","name":{}}',
                        b'{"type":"switch-link","version":1,"ftp_port":1e309}',
                        b'[' * 1500 + b']' * 1500):
            self.assertIsNone(decode_beacon(payload, '192.168.1.75'))
        result = decode_beacon(b'{"type":"switch-link","version":1,"ftp_port":5000,"host":"1.1.1.1","device_id":"local"}', '192.168.1.75')
        self.assertEqual(result['host'], '192.168.1.75')

    def test_stale_confirmation_cannot_delete_same_path_on_another_device(self):
        root = Path(self.tmp.name) / 'other-console'
        root.mkdir()
        protected = root / 'same-name.txt'
        protected.write_bytes(b'belongs to the other console')
        authorizer = DummyAuthorizer()
        authorizer.add_anonymous(str(root), perm='elradfmwMT')
        handler = type('OtherConsoleFTP', (FTPHandler,), {'authorizer': authorizer})
        ftp = FTPServer(('127.0.0.1', 0), handler)
        ready = threading.Event()
        ftp.ioloop.call_later(0, ready.set)
        thread = threading.Thread(target=ftp.serve_forever,
                                  kwargs={'timeout': .05, 'handle_exit': False}, daemon=True)
        thread.start()
        self.assertTrue(ready.wait(3), 'Local FTP event loop did not start')
        try:
            self.app.discovery.devices['B'] = {'id': 'B', 'ip': '127.0.0.1', 'port': ftp.socket.getsockname()[1],
                                                'connected': True, 'last_seen': time.time()}
            self.app.selected_id = 'B'
            code, _ = self.request('DELETE', '/api/files?path=/same-name.txt&expected_device=A')
            self.assertEqual(code, 409)
            self.assertEqual(protected.read_bytes(), b'belongs to the other console')
        finally:
            ftp.close_all()
            thread.join(timeout=2)


if __name__ == '__main__':
    unittest.main()
