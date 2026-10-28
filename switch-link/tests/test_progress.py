import json
import socket
import unittest

from switch_link.progress import TaskPublisher


class ProgressTests(unittest.TestCase):
    def setUp(self):
        self.receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.receiver.bind(('127.0.0.1', 0))
        self.receiver.settimeout(.15)
        self.publisher = TaskPublisher({'id': 'a' * 32, 'mode': 'switch-link', 'ip': '127.0.0.1',
                                        'progress_port': self.receiver.getsockname()[1]})

    def tearDown(self):
        self.publisher.close()
        self.receiver.close()

    def test_console_receives_real_byte_counts_and_stages_without_pc_source_path(self):
        job = {'id': 'b' * 32, 'name': 'own.nsp', 'kind': 'install', 'bytes_done': 25,
               'bytes_total': 100, 'speed': 5, 'progress': 25}
        self.assertTrue(self.publisher.send(job, 'transferring', force=True))
        message = json.loads(self.receiver.recv(4096))
        self.assertEqual(message['device_id'], 'a' * 32)
        self.assertEqual(message['bytes_done'], 25)
        self.assertEqual(message['bytes_total'], 100)
        self.assertEqual(message['wire_name'], 'b' * 32 + '_own.nsp')
        self.assertEqual(message['stage'], 'transferring')
        self.assertFalse(self.publisher.send(job, 'transferring'))
        self.assertTrue(self.publisher.send(job, 'registering', force=True))
        self.assertEqual(json.loads(self.receiver.recv(4096))['stage'], 'registering')

    def test_legacy_client_is_not_sent_unsupported_progress_packets(self):
        legacy = TaskPublisher({'id': 'legacy', 'mode': 'dbi', 'ip': '127.0.0.1',
                                'progress_port': self.receiver.getsockname()[1]})
        self.assertFalse(legacy.send({'id': 'b' * 32, 'name': 'x', 'kind': 'upload'}, 'transferring', force=True))
        with self.assertRaises(socket.timeout):
            self.receiver.recv(4096)
        legacy.close()


if __name__ == '__main__':
    unittest.main()
