from pathlib import Path
import json
import tempfile
import threading
import time
import unittest

from switch_link.jobs import JobManager
from switch_link.transport import RemoteError


class FilePeer:
    """The console protocol boundary: returns real byte counts and optional receipts."""
    def __init__(self, receipt=None):
        self.receipt = receipt
        self.content = None

    def upload(self, source, target, overwrite=False, progress=None, cancel=None):
        self.content = Path(source).read_bytes()
        if progress:
            progress(len(self.content))
        return {'bytes': len(self.content), 'backup': None}

    def install(self, source, name, progress=None, cancel=None):
        self.content = Path(source).read_bytes()
        if progress:
            progress(len(self.content))
        return self.receipt


class JobTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.source = Path(self.tmp.name) / 'test.nsp'
        self.source.write_bytes(b'own test data')
        self.peer = FilePeer()
        self.jobs = JobManager(lambda: self.peer, Path(self.tmp.name))

    def tearDown(self):
        self.jobs.close()
        self.tmp.cleanup()

    def wait(self, job):
        end = time.monotonic() + 3
        while time.monotonic() < end:
            current = self.jobs.get(job['id'])
            if current['state'] not in ('queued', 'transferring'):
                return current
            time.sleep(.01)
        self.fail('Job never left its active state')

    def test_ftp_transfer_without_install_receipt_is_not_reported_installed(self):
        job = self.jobs.submit('install', self.source, 'test.nsp')
        result = self.wait(job)
        self.assertEqual(result['state'], 'awaiting_confirmation')
        self.assertEqual(result['bytes_done'], 13)

    def test_explicit_console_install_success_is_reported_completed(self):
        self.peer.receipt = {'success': True, 'result': 0}
        result = self.wait(self.jobs.submit('install', self.source, 'test.nsp'))
        self.assertEqual(result['state'], 'completed')

    def test_console_install_error_is_reported_failed_even_after_full_transfer(self):
        self.peer.receipt = {'success': False, 'result': 123, 'message': 'Not enough SD space'}
        result = self.wait(self.jobs.submit('install', self.source, 'test.nsp'))
        self.assertEqual(result['state'], 'failed')
        self.assertIn('Not enough SD space', result['error'])

    def test_completed_file_upload_preserves_the_received_content(self):
        result = self.wait(self.jobs.submit('upload', self.source, 'test.nsp', path='/switch/test.nsp'))
        self.assertEqual(result['state'], 'completed')
        self.assertEqual(self.peer.content, b'own test data')

    def test_completed_job_cleans_only_its_owned_inbox_copy(self):
        inbox = Path(self.tmp.name) / 'inbox'
        inbox.mkdir()
        staged = inbox / 'staged.bin'
        staged.write_bytes(b'upload')
        job = self.jobs.submit('upload', staged, 'own.nro', path='/switch/own.nro')
        self.wait(job)
        deadline = time.monotonic() + 2
        while staged.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertFalse(staged.exists())
        self.assertTrue(self.source.exists())
        self.assertNotIn(job['id'], self.jobs.sources)
        self.assertNotIn(job['id'], self.jobs.cancel_events)

    def test_cancelled_waiting_job_also_releases_its_inbox_copy(self):
        gate, started = threading.Event(), threading.Event()
        original_upload = self.peer.upload
        def gated_upload(*args, **kwargs):
            started.set()
            gate.wait(2)
            return original_upload(*args, **kwargs)
        self.peer.upload = gated_upload
        first = self.jobs.submit('upload', self.source, 'first.nro', path='/switch/first.nro')
        self.assertTrue(started.wait(1))
        inbox = Path(self.tmp.name) / 'inbox'
        inbox.mkdir()
        staged = inbox / 'cancel.bin'
        staged.write_bytes(b'cancel')
        second = self.jobs.submit('upload', staged, 'second.nro', path='/switch/second.nro')
        self.jobs.cancel(second['id'])
        gate.set()
        self.wait(first)
        deadline = time.monotonic() + 2
        while staged.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertFalse(staged.exists())
        self.assertEqual(self.jobs.get(second['id'])['state'], 'cancelled')


if __name__ == '__main__':
    unittest.main()
