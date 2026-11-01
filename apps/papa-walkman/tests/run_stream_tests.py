"""Run real HTTP behavior with libnx's unsupported pthread_detach contract."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parent
executable, shim = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="papa-http-") as folder:
    work = Path(folder)
    (work / "build-host/stream").mkdir(parents=True)
    (work / "sdmc:/config/papa-walkman/cache").mkdir(parents=True)
    (work / "tests").mkdir()
    shutil.copy(root / "tone.mp3", work / "tests/tone.mp3")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    server = subprocess.Popen([sys.executable, str(root / "stream_server.py"),
                               "tests/tone.mp3", str(port)], cwd=work)
    try:
        deadline = time.monotonic() + 5
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1):
                    break
            except OSError:
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("HTTP fixture server did not start")
                time.sleep(.02)
        env = dict(os.environ)
        env["DYLD_INSERT_LIBRARIES" if sys.platform == "darwin" else "LD_PRELOAD"] = shim
        for case in ["prefetch", None, "long", "fragment", "late", "late-cancel", "range-short", "seek", "shutdown"]:
            command = [executable, f"http://127.0.0.1:{port}"]
            if case:
                command.append(case)
            subprocess.run(command, cwd=work, env=env, check=True, timeout=90)
    finally:
        server.terminate()
        server.wait(timeout=5)
