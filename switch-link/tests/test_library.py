import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.request import build_opener, ProxyHandler, Request
from urllib.error import HTTPError

from switch_link.library import Library, LibraryError, HttpShare


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.library = Library(Path(self.tmp.name) / 'library')
        self.share = HttpShare(self.library, host='127.0.0.1', port=0, advertise='127.0.0.1')
        self.share.start()
        self.base = self.share.url
        self.opener = build_opener(ProxyHandler({}))

    def tearDown(self):
        self.share.close()
        self.tmp.cleanup()

    def get(self, path, **headers):
        return self.opener.open(Request(self.base + path, headers=headers), timeout=3)

    def test_publication_needs_no_console_and_rejects_collision_or_partial_content(self):
        entry = self.library.receive('自己的 & 游戏.nsp', io.BytesIO(b'owned data'), 10)
        self.assertEqual(entry['state'], 'ready')
        self.assertEqual(entry['size'], 10)
        with self.assertRaises(LibraryError): self.library.receive(entry['name'], io.BytesIO(b'new'), 3)
        with self.assertRaises(LibraryError): self.library.receive('partial.nsp', io.BytesIO(b'x'), 10)
        self.assertEqual([e['name'] for e in self.library.files()], [entry['name']])
        self.assertFalse(any(p.name.startswith('.') for p in self.library.root.iterdir()))

    def test_directory_and_range_stream_exact_bytes_without_reporting_installed(self):
        self.library.receive('自己的.nsp', io.BytesIO(b'0123456789'), 10)
        with self.get('/files/') as r:
            html = r.read().decode()
            self.assertIn('%E8%87%AA%E5%B7%B1%E7%9A%84.nsp', html)
        with self.get('/files/%E8%87%AA%E5%B7%B1%E7%9A%84.nsp', Range='bytes=2-5', **{'User-Agent':'DBI/921'}) as r:
            self.assertEqual(r.status, 206)
            self.assertEqual(r.headers['Content-Range'], 'bytes 2-5/10')
            self.assertEqual(r.read(), b'2345')
        status = self.share.snapshot()
        self.assertTrue(status['available'])
        self.assertEqual(status['transfers'][0]['state'], 'sent')
        self.assertEqual(status['transfers'][0]['bytes_done'], 4)
        self.assertNotIn('installed', json.dumps(status))
        with self.get('/files/%E8%87%AA%E5%B7%B1%E7%9A%84.nsp', Range='bytes=-3') as r: self.assertEqual(r.read(), b'789')
        req = Request(self.base+'/files/%E8%87%AA%E5%B7%B1%E7%9A%84.nsp', method='HEAD')
        with self.opener.open(req) as r: self.assertEqual(r.headers['Content-Length'], '10'); self.assertEqual(r.read(), b'')

    def test_unrelated_browser_requests_are_not_dbi_connection_evidence(self):
        with self.get('/files/', **{'User-Agent':'Mozilla/5.0 DBI documentation'}): pass
        self.assertIsNone(self.share.snapshot()['switch_access'])
        with self.get('/files/', **{'User-Agent':'DBI/921'}): pass
        self.assertEqual(self.share.snapshot()['switch_access']['ip'], '127.0.0.1')

    def test_range_and_paths_cannot_escape_or_publish_non_packages(self):
        self.library.receive('test.nsp', io.BytesIO(b'abc'), 3)
        for path in ['/files/../secret', '/files/%2e%2e%2fsecret', '/files/test.nsp/extra']:
            with self.subTest(path=path), self.assertRaises(HTTPError): self.get(path)
        with self.assertRaises(HTTPError) as caught: self.get('/files/test.nsp', Range='bytes=30-40')
        self.assertEqual(caught.exception.code, 416)
        for name in ['../owned.nsp', '.hidden.nsp', 'prod.keys', 'nested/x.nsp', 'x\\y.nsp', 'foo:bar.nsp']:
            with self.subTest(name=name), self.assertRaises(LibraryError): self.library.receive(name, io.BytesIO(b'a'), 1)

    def test_second_share_reuses_compatible_live_service_and_cannot_close_it(self):
        attached = HttpShare(self.library, host='127.0.0.1', port=self.share.port, advertise='127.0.0.1')
        attached.start()
        self.assertFalse(attached.owned)
        self.assertTrue(attached.snapshot()['available'])
        attached.close()
        with self.get('/files/') as r: self.assertEqual(r.status, 200)

    def test_legacy_waiting_sentinel_is_not_a_switch_visit(self):
        attached = HttpShare(self.library, host='127.0.0.1', port=self.share.port, advertise='127.0.0.1')
        attached.external = True
        with patch.object(attached, '_existing', return_value={'switch_connection': {'http': {'state': 'waiting'}}}):
            self.assertIsNone(attached.snapshot()['switch_access'])

    def test_incompatible_json_status_does_not_crash_or_take_over_an_existing_port(self):
        for payload in (b'[]', b'null', b'"another application"'):
            other = HttpShare(self.library, host='127.0.0.1', port=self.share.port, advertise='127.0.0.1')
            with self.subTest(payload=payload), patch.object(other.opener, 'open', return_value=io.BytesIO(payload)):
                other.start()
                self.assertFalse(other.owned)
                self.assertFalse(other.snapshot()['available'])
                self.assertTrue(other.error)
        with self.get('/files/') as r: self.assertEqual(r.status, 200)


if __name__ == '__main__': unittest.main()
