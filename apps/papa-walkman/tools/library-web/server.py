#!/usr/bin/env python3
"""Local read-only tester. No dependencies, no changes to the music server."""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent
ALLOWED_HOST = 'kiro.papaclaoud.top'
HEADERS = ['Content-Type', 'Content-Length', 'Content-Range', 'Accept-Ranges', 'ETag', 'Last-Modified', 'Cache-Control', 'Access-Control-Allow-Origin', 'Access-Control-Expose-Headers']


def valid_url(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.hostname != ALLOWED_HOST or parsed.username or parsed.password or parsed.port not in (None, 443):
        raise ValueError('本地检查只允许访问 https://kiro.papaclaoud.top')
    return url


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        valid_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


OPENER = urllib.request.build_opener(SafeRedirect())


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def json_response(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path not in ('/api/fetch', '/api/check'):
            if parsed.path not in ('/', '/index.html', '/app.mjs', '/core.mjs', '/style.css', '/favicon.ico'):
                self.send_error(404)
                return
            return super().do_GET()
        query = urllib.parse.parse_qs(parsed.query)
        url = query.get('url', [''])[0]
        try:
            valid_url(url)
            probe = parsed.path == '/api/check'
            req = urllib.request.Request(url, headers={'User-Agent': 'Papa-Walkman-Library-Tester/1.0', 'Range': 'bytes=0-127'} if probe else {'User-Agent': 'Papa-Walkman-Library-Tester/1.0'})
            with OPENER.open(req, timeout=10) as upstream:
                valid_url(upstream.url)
                limit = 512 if probe else 8 * 1024 * 1024
                body = upstream.read(limit + 1)
                if not probe and len(body) > limit:
                    raise ValueError('资源超过 8 MiB 检查上限')
                result = {'ok': True, 'status': upstream.status, 'url': upstream.url, 'headers': {k: upstream.headers.get(k) for k in HEADERS}, 'bytes': len(body)}
                if not probe:
                    result['text'] = body.decode('utf-8-sig')
                else:
                    kind = query.get('kind', [''])[0]
                    if kind == 'cover':
                        result['valid'] = body.startswith(b'\xff\xd8') or body.startswith(b'\x89PNG\r\n\x1a\n')
                        result['message'] = 'JPEG / PNG 图片' if result['valid'] else '不是可识别的 JPEG / PNG'
                    elif kind == 'audio':
                        result['valid'] = upstream.status == 206 and (upstream.headers.get('Content-Range') or '').startswith('bytes 0-')
                        result['message'] = '支持 Range 206' if result['valid'] else '未验证到正确的 Range 206'
                    else:
                        sample = body.decode('utf-8', errors='replace').lstrip()
                        result['valid'] = bool(sample) and not sample.lower().startswith(('<html', '<!doctype'))
                        result['message'] = '返回非空文本' if result['valid'] else '内容为空或返回网页'
                self.json_response(result)
        except urllib.error.HTTPError as exc:
            self.json_response({'ok': False, 'status': exc.code, 'url': url, 'error': f'HTTP {exc.code}'})
        except (OSError, ValueError, UnicodeError) as exc:
            self.json_response({'ok': False, 'url': url, 'error': str(exc)}, 400)

    def log_message(self, fmt, *args):
        if not self.path.startswith('/api/'):
            super().log_message(fmt, *args)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8769)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print(f'Kiro 曲库测试页：http://127.0.0.1:{args.port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
