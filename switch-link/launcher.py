"""Open the companion, starting its local-only service when needed."""
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.request
import webbrowser

URL = 'http://127.0.0.1:8088/'
ROOT = Path(__file__).resolve().parent


def is_running():
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(URL + 'api/status', timeout=1) as response:
            return json.load(response).get('app') == 'Switch Link'
    except (OSError, ValueError):
        return False


def main():
    if not is_running():
        data = ROOT / 'data'
        data.mkdir(exist_ok=True)
        flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
        with (data / 'service.log').open('ab') as log:
            subprocess.Popen([sys.executable, '-X', 'utf8', '-u', str(ROOT / 'server.py')],
                             cwd=ROOT, stdout=log, stderr=log, creationflags=flags)
        for _ in range(50):
            if is_running():
                break
            time.sleep(.2)
        else:
            raise SystemExit('Switch Link could not start. See data/service.log.')
    webbrowser.open(URL + '#install')


if __name__ == '__main__':
    main()
