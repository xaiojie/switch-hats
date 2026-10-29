import unittest
import test_api


class LibraryApiTests(unittest.TestCase):
    setUp = test_api.ApiTests.setUp
    tearDown = test_api.ApiTests.tearDown
    request = test_api.ApiTests.request
    def test_http_library_publication_works_without_a_ftp_device(self):
        code, info = self.request('GET', '/api/library')
        self.assertEqual(code, 200)
        self.assertFalse(info['available'])
        identity = info['library_id']
        code, data = self.request('PUT', '/api/library/upload?name=owned.nsp&expected_library='+identity, b'owned')
        self.assertEqual(code, 201)
        self.assertEqual(data['file']['state'], 'ready')
        self.assertEqual(self.app.jobs.snapshot(), [])
        self.assertEqual((self.app.library.root/'owned.nsp').read_bytes(), b'owned')
        code, status = self.request('GET', '/api/status')
        self.assertIsNone(status['device'])
        self.assertEqual(status['http_library']['files'][0]['name'], 'owned.nsp')

    def test_stale_library_identity_and_origin_cannot_publish(self):
        code, _ = self.request('PUT', '/api/library/upload?name=owned.nsp&expected_library=old', b'owned')
        self.assertEqual(code, 409)
        code, _ = self.request('PUT', '/api/library/upload?name=owned.nsp&expected_library='+self.app.library.id,
                               b'owned', {'Origin':'https://unrelated.example'})
        self.assertEqual(code, 403)
        self.assertEqual(self.app.library.files(), [])

    def test_old_ftp_install_endpoint_cannot_start_an_installation(self):
        code, data = self.request('PUT', '/api/install?name=owned.nsp', b'owned')
        self.assertEqual(code, 410)
        self.assertIn('HTTP', data['error'])
        self.assertEqual(self.app.jobs.snapshot(), [])

    def test_preflight_rejection_returns_a_readable_error_without_resetting_a_sent_body(self):
        for _ in range(40):
            code, data = self.request('PUT', '/api/install?name=owned.nsp', b'x' * 65536)
            self.assertEqual(code, 410)
            self.assertIn('HTTP', data['error'])
