import http.client
import importlib.util
import json
from pathlib import Path
import socket
import socketserver
import tempfile
import threading
import time
import unittest


spec = importlib.util.spec_from_file_location("library_server", Path(__file__).with_name("server.py"))
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)


class FtpFixture(socketserver.StreamRequestHandler):
    """A local FTP peer exercising ftplib login and directory listing over sockets."""

    def handle(self):
        data_listener = None
        self.wfile.write(b"220 Local test FTP\r\n")
        try:
            for raw in self.rfile:
                command = raw.decode().strip().split(" ", 1)[0]
                if command == "USER":
                    reply = b"331 Password required\r\n"
                elif command == "PASS":
                    reply = b"230 Logged in\r\n"
                elif command == "TYPE":
                    reply = b"200 Type set\r\n"
                elif command == "PASV":
                    data_listener = socket.socket()
                    data_listener.bind(("127.0.0.1", 0))
                    data_listener.listen(1)
                    port = data_listener.getsockname()[1]
                    reply = ("227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n" %
                             (port // 256, port % 256)).encode()
                elif command == "MLSD":
                    self.wfile.write(b"150 Opening data connection\r\n")
                    conn, _ = data_listener.accept()
                    with conn:
                        conn.sendall(b"type=dir;modify=20261028010000; switch\r\n")
                    data_listener.close()
                    data_listener = None
                    reply = b"226 Directory sent\r\n"
                elif command == "QUIT":
                    self.wfile.write(b"221 Goodbye\r\n")
                    return
                else:
                    reply = b"502 Unsupported\r\n"
                self.wfile.write(reply)
        finally:
            if data_listener is not None:
                data_listener.close()


class ConnectionTests(unittest.TestCase):
    def setUp(self):
        self.library = tempfile.TemporaryDirectory()
        app.ROOT = self.library.name
        app.HTTP_CONNECTION = None
        app.FTP_CONNECTION = None
        app.ACTIVITY.clear()
        self.httpd = app.ThreadingHTTPServer(("127.0.0.1", 0), app.Handler)
        app.PORT = self.httpd.server_port
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join()
        self.library.cleanup()

    def request(self, path, user_agent="Mozilla/5.0", method="GET", headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_port, timeout=3)
        try:
            conn.request(method, path, headers={"User-Agent": user_agent, **(headers or {})})
            response = conn.getresponse()
            return response.status, response.read()
        finally:
            conn.close()

    def status(self):
        code, body = self.request("/api/status")
        self.assertEqual(code, 200)
        result = json.loads(body)
        self.assertIn("switch_connection", result, "Status must report Switch connection evidence")
        return result

    def test_browser_and_generic_clients_do_not_confirm_a_switch(self):
        for ua in ("Mozilla/5.0", "curl/8.0", "python-requests/2.0", ""):
            self.request("/files/", ua)
        state = self.status()["switch_connection"]
        self.assertEqual(state["http"]["state"], "waiting")
        self.assertEqual(state["ftp"]["state"], "waiting")

    def test_dbi_library_request_records_peer_path_and_time(self):
        self.request("/files/", "DBI/921")
        state = self.status()["switch_connection"]["http"]
        self.assertEqual(state["state"], "recent")
        self.assertEqual(state["ip"], "127.0.0.1")
        self.assertEqual(state["path"], "/files/")
        self.assertEqual(state["status"], 200)
        self.assertLess(state["seconds_ago"], 3)
        self.assertGreater(state["last_seen"], 0)

    def test_dbi_status_poll_does_not_fake_a_library_connection(self):
        self.request("/api/status", "DBI/921")
        self.assertEqual(self.status()["switch_connection"]["http"]["state"], "waiting")

    def test_browser_requests_do_not_erase_last_dbi_evidence(self):
        self.request("/files/", "DBI/921")
        for _ in range(12):
            self.request("/files/")
        self.assertEqual(self.status()["switch_connection"]["http"]["state"], "recent")

    def test_old_visit_is_not_presented_as_current_connection(self):
        self.request("/files/", "DBI/921")
        self.status()
        with app.LOCK:
            app.HTTP_CONNECTION["last_seen"] = time.time() - 180
        self.assertEqual(self.status()["switch_connection"]["http"]["state"], "idle")

    def test_failed_request_is_visible_instead_of_success(self):
        code, _ = self.request("/files/missing.nsp", "DBI/921")
        self.assertEqual(code, 404)
        state = self.status()["switch_connection"]["http"]
        self.assertEqual(state["state"], "error")
        self.assertEqual(state["status"], 404)

    def test_range_download_still_works_and_counts_as_dbi_access(self):
        Path(self.library.name, "own-backup.nsp").write_bytes(b"abcdefgh")
        code, data = self.request("/files/own-backup.nsp", "DBI/921", headers={"Range": "bytes=2-4"})
        self.assertEqual((code, data), (206, b"cde"))
        self.assertEqual(self.status()["switch_connection"]["http"]["path"], "/files/own-backup.nsp")

    def test_ftp_success_and_subsequent_failure_are_independent_of_http(self):
        ftp = socketserver.TCPServer(("127.0.0.1", 0), FtpFixture)
        ftp_thread = threading.Thread(target=ftp.serve_forever, daemon=True)
        ftp_thread.start()
        port = ftp.server_address[1]
        try:
            code, body = self.request("/api/sw/list?host=127.0.0.1&port=%d&path=/" % port)
            self.assertEqual(code, 200)
            self.assertEqual(json.loads(body)["items"][0]["name"], "switch")
            state = self.status()["switch_connection"]
            self.assertEqual(state["ftp"]["state"], "recent")
            self.assertEqual(state["ftp"]["ip"], "127.0.0.1")
            self.assertEqual(state["http"]["state"], "waiting")
        finally:
            ftp.shutdown()
            ftp.server_close()
            ftp_thread.join()
        code, _ = self.request("/api/sw/list?host=127.0.0.1&port=%d&path=/" % port)
        self.assertEqual(code, 502)
        state = self.status()["switch_connection"]["ftp"]
        self.assertEqual(state["state"], "error")
        self.assertTrue(state["error"])


if __name__ == "__main__":
    unittest.main()
