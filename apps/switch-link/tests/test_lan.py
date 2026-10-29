import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

import server as entrypoint
from switch_link.http import Application, make_server


class LanAccessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.app = Application(Path(self.temporary.name), discover=False)
        self.server = make_server(self.app, '0.0.0.0', 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.app.close()
        self.temporary.cleanup()

    def request(self, method, path, headers=None, body=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def test_default_launch_binds_lan_and_loopback(self):
        self.assertEqual(entrypoint.build_parser().parse_args([]).host, '0.0.0.0')
        self.assertEqual(entrypoint.build_parser().parse_args(['--host', '127.0.0.1']).host,
                         '127.0.0.1')

    def test_status_advertises_reachable_lan_address_not_wildcard(self):
        with patch('switch_link.http.local_addresses', return_value=['192.168.1.169']):
            code, state = self.request('GET', '/api/status')
        self.assertEqual(code, 200)
        self.assertEqual(state['lan_url'], f'http://192.168.1.169:{self.server.server_port}/')
        self.assertEqual(state['local_url'], f'http://127.0.0.1:{self.server.server_port}/')

    def test_lan_browser_same_origin_can_operate(self):
        host = f'192.168.1.169:{self.server.server_port}'
        code, state = self.request('POST', '/api/connect',
                                   {'Host': host, 'Origin': 'http://' + host},
                                   '{"id":"not-a-device"}')
        self.assertEqual(code, 404)  # Device lookup ran; it was not rejected as cross-origin.
        self.assertIn('设备', state['error'])

    def test_cross_origin_stays_rejected_on_lan(self):
        host = f'192.168.1.169:{self.server.server_port}'
        code, _ = self.request('POST', '/api/connect',
                               {'Host': host, 'Origin': 'https://untrusted.example'},
                               '{"id":"not-a-device"}')
        self.assertEqual(code, 403)

    def test_explicit_loopback_mode_does_not_advertise_unreachable_lan(self):
        self.app.host = '127.0.0.1'
        with patch('switch_link.http.local_addresses', return_value=['192.168.1.169']):
            code, state = self.request('GET', '/api/status')
        self.assertEqual(code, 200)
        self.assertEqual(state['lan_url'], '')
        self.assertEqual(state['local_url'], f'http://127.0.0.1:{self.server.server_port}/')


if __name__ == '__main__':
    unittest.main()
