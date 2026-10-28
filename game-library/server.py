#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
爬爬的 Switch 游戏库 —— 配合 DBI 的 Home server 使用。

用法：
    python3 server.py                 # 默认端口 8080，库目录 ~/SwitchGames/游戏库
    python3 server.py --port 8081
    python3 server.py --dir /path/to/games

它在局域网里提供两套东西：
    /            管理面板（网页：上传、重命名、删除、看连接信息）
    /files/      DBI 用的目录列表（Apache 风格 <a href> 链接，DBI 的 ApacheHTTP 类型能解析）
    /files/xxx   NSP/XCI 文件本体，支持 HTTP Range 断点续传
"""

import argparse
import ftplib
import html
import json
import os
import re
import shutil
import socket
import sys
import threading
import time
import urllib.parse
import uuid
from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

APP = "爬爬的 Switch 游戏库"
ROOT = ""
PORT = 8080
STARTED = time.time()
LOCK = threading.Lock()
ACTIVITY = deque(maxlen=10)

CHUNK = 1024 * 1024


# ---------------------------------------------------------------- 工具函数

def human_size(n):
    if n is None:
        return "-"
    units = ["B", "KB", "MB", "GB", "TB"]
    f = float(n)
    for u in units:
        if f < 1024 or u == units[-1]:
            return ("%d %s" % (f, u)) if u == "B" else ("%.1f %s" % (f, u))
        f /= 1024.0


def all_ips():
    ips = []
    # 主出口 IP（UDP connect 不会真的发包）
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.2)
        s.connect(("8.8.8.8", 80))
        ips.append(s.getsockname()[0])
    except Exception:
        pass
    finally:
        try:
            s.close()
        except Exception:
            pass
    # 主机名解析出来的其它地址
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip not in ips:
                ips.append(ip)
    except Exception:
        pass
    out = []
    for ip in ips:
        if ip.startswith("127.") or ip.startswith("169.254.") or ip in out:
            continue
        out.append(ip)
    return out or ["127.0.0.1"]


def safe_name(raw):
    """只允许库根目录下的单层文件名，挡掉路径穿越。"""
    if raw is None:
        return None
    name = urllib.parse.unquote(raw).strip()
    name = name.replace("\\", "/").split("/")[-1]
    if not name or name in (".", ".."):
        return None
    if len(name) > 200:
        return None
    if any(ord(c) < 32 for c in name):
        return None
    if name.startswith("."):
        return None
    return name


def path_for(name):
    return os.path.join(ROOT, name)


def list_files():
    items = []
    try:
        for entry in os.scandir(ROOT):
            if entry.name.startswith("."):
                continue
            if not entry.is_file(follow_symlinks=False):
                continue
            st = entry.stat()
            items.append({
                "name": entry.name,
                "size": st.st_size,
                "mtime": int(st.st_mtime),
            })
    except FileNotFoundError:
        os.makedirs(ROOT, exist_ok=True)
    items.sort(key=lambda x: x["name"].lower())
    return items


def record(method, path, status, ua):
    entry = {
        "time": datetime.now().strftime("%H:%M:%S"),
        "method": method,
        "path": path,
        "status": status,
        "ua": (ua or "")[:48],
    }
    with LOCK:
        ACTIVITY.appendleft(entry)


# ---------------------------------------------------------------- Switch 文件（DBI FTP）

class LimitedReader(object):
    """按 Content-Length 限流地读 HTTP 请求体，喂给 ftplib。"""

    def __init__(self, rfile, length):
        self.rfile = rfile
        self.left = length

    def read(self, n=-1):
        if self.left <= 0:
            return b""
        if n is None or n < 0 or n > self.left:
            n = self.left
        data = self.rfile.read(n)
        if not data:
            self.left = 0
            return b""
        self.left -= len(data)
        return data


def sw_path(raw):
    """规范化 Switch 上的 POSIX 路径，挡掉 .. 和控制字符。"""
    p = urllib.parse.unquote((raw or "/").strip()).replace("\\", "/")
    if "\x00" in p:
        return None
    parts = []
    for seg in p.split("/"):
        if seg in ("", "."):
            continue
        if seg == ".." or any(ord(c) < 32 for c in seg):
            return None
        parts.append(seg)
    return "/" + "/".join(parts)


def sw_name(raw):
    name = urllib.parse.unquote((raw or "").strip())
    if not name or name in (".", "..") or "/" in name or "\\" in name:
        return None
    if any(ord(c) < 32 for c in name):
        return None
    return name


def sw_join(base, name):
    return ("/" + name) if base in ("", "/") else (base.rstrip("/") + "/" + name)


def sw_connect(host, port):
    port = int(port or 5000)
    conn = ftplib.FTP()
    conn.encoding = "utf-8"
    conn.connect(host, port, timeout=10)
    conn.login("anonymous", "anonymous@")
    conn.set_pasv(True)
    return conn


def sw_parse_modify(s):
    if not s:
        return 0
    try:
        return int(datetime.strptime(s.split(".")[0], "%Y%m%d%H%M%S").timestamp())
    except Exception:
        return 0


UNIX_LS = re.compile(
    r"^([dl-])[rwxstST-]{9}[.+@]?\s+\d+\s+\S+\s+\S+\s+(\d+)\s+"
    r"(\w{3}\s+\d{1,2}\s+[\d:]{4,5})\s+(.+)$")
DOS_LS = re.compile(
    r"^(\d{2})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2})\s*(AM|PM)\s+(<DIR>|\d+)\s+(.+)$",
    re.I)


def sw_list_raw(conn, path):
    """MLSD 不可用时的兜底：解析 LIST 输出。"""
    lines = []
    conn.retrlines("LIST " + path, lines.append)
    items, now = [], datetime.now()
    for line in lines:
        line = line.strip()
        m = UNIX_LS.match(line)
        if m:
            kind, size, when, name = m.groups()
            mtime = 0
            try:
                dt = datetime.strptime(when, "%b %d %H:%M").replace(year=now.year)
                if dt > now:
                    dt = dt.replace(year=now.year - 1)
                mtime = int(dt.timestamp())
            except Exception:
                pass
            if name not in (".", ".."):
                items.append({"name": name, "dir": kind == "d",
                              "size": int(size or 0), "mtime": mtime})
            continue
        m = DOS_LS.match(line)
        if m:
            _mo, dd, yy, hh, mm, ap, size, name = m.groups()
            try:
                hour = int(hh) % 12 + (12 if ap.upper() == "PM" else 0)
                mtime = int(datetime(2000 + int(yy), int(dd), int(_mo),
                                     hour, int(mm)).timestamp())
            except Exception:
                mtime = 0
            is_dir = size.upper() == "<DIR>"
            items.append({"name": name, "dir": is_dir,
                          "size": 0 if is_dir else int(size), "mtime": mtime})
    return items


def sw_list(conn, path):
    items = []
    try:
        for name, facts in conn.mlsd(path):
            typ = (facts.get("type") or "").lower()
            if name in (".", "..") or typ in ("cdir", "pdir"):
                continue
            items.append({
                "name": name,
                "dir": typ == "dir",
                "size": int(facts.get("size") or 0),
                "mtime": sw_parse_modify(facts.get("modify")),
            })
    except (ftplib.error_perm, ftplib.error_proto, ftplib.error_temp):
        items = sw_list_raw(conn, path)
    items.sort(key=lambda x: (not x["dir"], x["name"].lower()))
    return items


# ---------------------------------------------------------------- HTTP 处理

class Handler(BaseHTTPRequestHandler):
    server_version = "SwitchLibrary/1.0"
    protocol_version = "HTTP/1.0"   # 简单可靠；Range 照样能用

    # ---- 基础响应
    def _send(self, status, body=b"", ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        if extra:
            for k, v in extra.items():
                self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def _json(self, obj, status=200):
        self._send(status, json.dumps(obj, ensure_ascii=False), "application/json; charset=utf-8")

    def _err(self, status, msg):
        self._json({"ok": False, "error": msg}, status)

    def _read_json(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return {}
        if n <= 0 or n > 1024 * 1024:
            return {}
        try:
            return json.loads(self.rfile.read(n).decode("utf-8"))
        except Exception:
            return {}

    # ---- 路由
    def do_GET(self):
        self._route("GET")

    def do_HEAD(self):
        self._route("HEAD")

    def do_PUT(self):
        self._route("PUT")

    def do_POST(self):
        self._route("POST")

    def _route(self, method):
        parsed = urllib.parse.urlsplit(self.path)
        path = urllib.parse.unquote(parsed.path)
        try:
            if path in ("/", "/index.html"):
                return self._send_ui()
            if path == "/favicon.ico":
                return self._send(204)
            if path == "/api/status":
                return self._status()
            if path == "/api/upload":
                return self._upload(parsed)
            if path == "/api/delete":
                return self._delete()
            if path == "/api/rename":
                return self._rename()
            if path == "/api/sw/list":
                return self._sw_list(parsed)
            if path == "/api/sw/upload":
                return self._sw_upload(parsed)
            if path == "/api/sw/download":
                return self._sw_download(parsed)
            if path == "/api/sw/mkdir":
                return self._sw_mkdir()
            if path == "/api/sw/delete":
                return self._sw_delete()
            if path == "/api/sw/rename":
                return self._sw_rename()
            if path == "/files":
                return self._send(301, b"", "text/plain; charset=utf-8",
                                  {"Location": "/files/"})
            if path == "/files/":
                return self._listing()
            if path.startswith("/files/"):
                return self._serve_file(path[len("/files/"):], head=(method == "HEAD"))
            return self._err(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as exc:  # 兜底，别让服务挂掉
            sys.stderr.write("[error] %s %s -> %r\n" % (method, self.path, exc))
            try:
                self._err(500, "server error")
            except Exception:
                pass

    # ---- 管理面板
    def _send_ui(self):
        body = PAGE.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _status(self):
        files = list_files()
        try:
            du = shutil.disk_usage(ROOT)
            free, total = du.free, du.total
        except Exception:
            free = total = 0
        with LOCK:
            acts = list(ACTIVITY)
        self._json({
            "app": APP,
            "port": PORT,
            "dir": ROOT,
            "ips": all_ips(),
            "files": files,
            "count": len(files),
            "size": sum(f["size"] for f in files),
            "free": free,
            "total": total,
            "activity": acts,
            "uptime": int(time.time() - STARTED),
        })

    # ---- 上传（原始 PUT，XHR 直传，支持大文件）
    def _upload(self, parsed):
        qs = urllib.parse.parse_qs(parsed.query)
        name = safe_name((qs.get("name") or [""])[0])
        if not name:
            return self._err(400, "文件名不合法")
        length = 0
        raw_len = self.headers.get("Content-Length")
        if raw_len:
            try:
                length = int(raw_len)
            except ValueError:
                return self._err(400, "Content-Length 不合法")
        if length <= 0:
            return self._err(411, "需要 Content-Length")

        dest = path_for(name)
        tmp = os.path.join(ROOT, ".upload-%s.part" % uuid.uuid4().hex)
        got = 0
        try:
            with open(tmp, "wb") as fh:
                while got < length:
                    chunk = self.rfile.read(min(CHUNK, length - got))
                    if not chunk:
                        break
                    fh.write(chunk)
                    got += len(chunk)
            if got != length:
                raise IOError("上传中断：%d/%d" % (got, length))
            os.replace(tmp, dest)
        except Exception as exc:
            try:
                if os.path.exists(tmp):
                    os.remove(tmp)
            except OSError:
                pass
            return self._err(500, str(exc))

        record("PUT", name, 201, self.headers.get("User-Agent"))
        self._json({"ok": True, "name": name, "size": got})

    def _delete(self):
        data = self._read_json()
        name = safe_name(data.get("name"))
        if not name:
            return self._err(400, "文件名不合法")
        target = path_for(name)
        if not os.path.isfile(target):
            return self._err(404, "文件不存在")
        try:
            os.remove(target)
        except OSError as exc:
            return self._err(500, str(exc))
        record("DELETE", name, 200, self.headers.get("User-Agent"))
        self._json({"ok": True})

    def _rename(self):
        data = self._read_json()
        old = safe_name(data.get("from"))
        new = safe_name(data.get("to"))
        if not old or not new:
            return self._err(400, "文件名不合法")
        src, dst = path_for(old), path_for(new)
        if not os.path.isfile(src):
            return self._err(404, "原文件不存在")
        if os.path.exists(dst):
            return self._err(409, "目标文件名已存在")
        try:
            os.rename(src, dst)
        except OSError as exc:
            return self._err(500, str(exc))
        record("RENAME", "%s -> %s" % (old, new), 200, self.headers.get("User-Agent"))
        self._json({"ok": True, "name": new})

    # ---- DBI 目录列表
    def _listing(self):
        items = list_files()
        rows = []
        for it in items:
            name = it["name"]
            href = urllib.parse.quote(name, safe="")
            size = human_size(it["size"])
            mtime = datetime.fromtimestamp(it["mtime"]).strftime("%Y-%m-%d %H:%M")
            rows.append('<a href="%s">%s</a>  %s  %s' %
                        (href, html.escape(name), size, mtime))
        body = (
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>Index of /files/</title></head>"
            "<body><h1>Index of /files/</h1><hr><pre>\n"
            + ("\n".join(rows) if rows else "(empty)")
            + "\n</pre><hr></body></html>"
        )
        self._send(200, body, "text/html; charset=utf-8")
        record("GET", "/files/", 200, self.headers.get("User-Agent"))

    # ---- 文件下载（Range）
    def _serve_file(self, raw_name, head=False):
        name = safe_name(raw_name)
        if not name:
            return self._err(400, "文件名不合法")
        target = path_for(name)
        if not os.path.isfile(target):
            return self._err(404, "文件不存在")
        size = os.path.getsize(target)
        start, end = 0, size - 1
        status = 200
        rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d*)-(\d*)$", rng.strip())
            if not m:
                return self._err(416, "Range 不合法")
            a, b = m.group(1), m.group(2)
            if a == "" and b == "":
                return self._err(416, "Range 不合法")
            if a == "":
                suffix = int(b)
                start = max(0, size - suffix)
            else:
                start = int(a)
                end = int(b) if b else size - 1
            if start >= size or start > end:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % size)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            end = min(end, size - 1)
            status = 206

        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.send_header("Content-Disposition",
                         "attachment; filename*=UTF-8''" + urllib.parse.quote(name, safe=""))
        self.end_headers()
        if head:
            record("HEAD", name, status, self.headers.get("User-Agent"))
            return
        try:
            with open(target, "rb") as fh:
                fh.seek(start)
                remaining = length
                while remaining > 0:
                    chunk = fh.read(min(CHUNK, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)
            record("GET", name, status, self.headers.get("User-Agent"))
        except (BrokenPipeError, ConnectionResetError):
            record("GET", name, 499, self.headers.get("User-Agent"))
            return

    # ---- Switch SD 文件管理（走 DBI 的 FTP 服务，端口 5000）
    def _sw_args(self, parsed):
        qs = urllib.parse.parse_qs(parsed.query)
        host = (qs.get("host") or [""])[0].strip()
        try:
            port = int((qs.get("port") or ["5000"])[0])
        except ValueError:
            port = 5000
        path = sw_path((qs.get("path") or ["/"])[0])
        if not host or not path:
            return None
        return host, port, path

    @staticmethod
    def _sw_close(conn):
        if conn is None:
            return
        try:
            conn.quit()
        except Exception:
            try:
                conn.close()
            except Exception:
                pass

    def _sw_list(self, parsed):
        args = self._sw_args(parsed)
        if not args:
            return self._err(400, "地址或路径不合法")
        host, port, path = args
        conn = None
        try:
            conn = sw_connect(host, port)
            items = sw_list(conn, path)
            self._json({"ok": True, "host": host, "port": port, "path": path,
                        "items": items, "count": len(items)})
            record("GET", "sw:" + path, 200, self.headers.get("User-Agent"))
        except Exception as exc:
            self._err(502, "连不上 %s:%s（%s）" % (host, port, exc))
            record("GET", "sw:" + path, 502, self.headers.get("User-Agent"))
        finally:
            self._sw_close(conn)

    def _sw_upload(self, parsed):
        args = self._sw_args(parsed)
        if not args:
            return self._err(400, "地址或路径不合法")
        host, port, path = args
        qs = urllib.parse.parse_qs(parsed.query)
        name = sw_name((qs.get("name") or [""])[0])
        if not name:
            return self._err(400, "文件名不合法")
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return self._err(400, "Content-Length 不合法")
        if length <= 0:
            return self._err(411, "需要 Content-Length")

        dest = sw_join(path, name)
        tmp = sw_join(path, ".upl-%s.part" % uuid.uuid4().hex[:8])
        conn = None
        try:
            conn = sw_connect(host, port)
            reader = LimitedReader(self.rfile, length)
            conn.storbinary("STOR " + tmp, reader, blocksize=CHUNK)
            if reader.left != 0:
                raise IOError("上传中断：还差 %d 字节" % reader.left)
            try:
                conn.delete(dest)
            except Exception:
                pass
            conn.rename(tmp, dest)
            record("PUT", "sw:" + dest, 201, self.headers.get("User-Agent"))
            self._json({"ok": True, "name": name, "size": length, "path": path})
        except Exception as exc:
            try:
                if conn is not None:
                    conn.delete(tmp)
            except Exception:
                pass
            self._err(502, "上传失败：%s" % exc)
            record("PUT", "sw:" + dest, 502, self.headers.get("User-Agent"))
        finally:
            self._sw_close(conn)

    def _sw_download(self, parsed):
        args = self._sw_args(parsed)
        if not args:
            return self._err(400, "地址或路径不合法")
        host, port, path = args
        conn = None
        try:
            conn = sw_connect(host, port)
            try:
                size = conn.size(path)
            except Exception:
                size = None
            name = path.rsplit("/", 1)[-1] or "download"
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            if size:
                self.send_header("Content-Length", str(size))
            self.send_header("Content-Disposition",
                             "attachment; filename*=UTF-8''" + urllib.parse.quote(name, safe=""))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            conn.retrbinary("RETR " + path, self.wfile.write, blocksize=CHUNK)
            record("GET", "sw-dl:" + path, 200, self.headers.get("User-Agent"))
        except (BrokenPipeError, ConnectionResetError):
            record("GET", "sw-dl:" + path, 499, self.headers.get("User-Agent"))
        except Exception as exc:
            record("GET", "sw-dl:" + path, 502, self.headers.get("User-Agent"))
            try:
                self._err(502, "下载失败：%s" % exc)
            except Exception:
                pass
        finally:
            self._sw_close(conn)

    def _sw_op_args(self):
        data = self._read_json()
        host = str(data.get("host") or "").strip()
        try:
            port = int(data.get("port") or 5000)
        except (TypeError, ValueError):
            port = 5000
        path = sw_path(data.get("path") or "/")
        if not host or not path:
            return None
        return data, host, port, path

    def _sw_mkdir(self):
        got = self._sw_op_args()
        if not got:
            return self._err(400, "地址或路径不合法")
        data, host, port, path = got
        name = sw_name(data.get("name") or "")
        if not name:
            return self._err(400, "文件夹名不合法")
        conn = None
        try:
            conn = sw_connect(host, port)
            conn.mkd(sw_join(path, name))
            record("MKDIR", "sw:" + sw_join(path, name), 200, self.headers.get("User-Agent"))
            self._json({"ok": True, "name": name, "path": path})
        except Exception as exc:
            self._err(502, "新建失败：%s" % exc)
        finally:
            self._sw_close(conn)

    def _sw_delete(self):
        got = self._sw_op_args()
        if not got:
            return self._err(400, "地址或路径不合法")
        data, host, port, path = got
        name = sw_name(data.get("name") or "")
        if not name:
            return self._err(400, "名字不合法")
        target = sw_join(path, name)
        is_dir = bool(data.get("dir"))
        conn = None
        try:
            conn = sw_connect(host, port)
            if is_dir:
                conn.rmd(target)
            else:
                try:
                    conn.delete(target)
                except ftplib.error_perm:
                    conn.rmd(target)
            record("DELETE", "sw:" + target, 200, self.headers.get("User-Agent"))
            self._json({"ok": True, "path": path})
        except Exception as exc:
            self._err(502, "删除失败：%s" % exc)
        finally:
            self._sw_close(conn)

    def _sw_rename(self):
        got = self._sw_op_args()
        if not got:
            return self._err(400, "地址或路径不合法")
        data, host, port, path = got
        old = sw_name(data.get("from") or "")
        new = sw_name(data.get("to") or "")
        if not old or not new:
            return self._err(400, "名字不合法")
        src, dst = sw_join(path, old), sw_join(path, new)
        conn = None
        try:
            conn = sw_connect(host, port)
            conn.rename(src, dst)
            record("RENAME", "sw:%s -> %s" % (src, new), 200, self.headers.get("User-Agent"))
            self._json({"ok": True, "name": new, "path": path})
        except Exception as exc:
            self._err(502, "改名失败：%s" % exc)
        finally:
            self._sw_close(conn)

    def log_message(self, fmt, *args):
        # 默认日志太吵，只写文件请求的简版
        pass


# ---------------------------------------------------------------- 页面

PAGE = r"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>爬爬的 Switch 游戏库</title>
<style>
  :root{
    --bg:#0a1220; --card:rgba(19,30,50,.82); --line:rgba(120,170,255,.16);
    --text:#e8f1ff; --dim:#8fa6c8; --accent:#4da3ff; --accent2:#7be2ff;
    --good:#47d7a1; --bad:#ff7a90;
  }
  *{box-sizing:border-box}
  body{
    margin:0; padding:32px 20px 60px; color:var(--text);
    font:14px/1.6 -apple-system,BlinkMacSystemFont,"PingFang SC","Hiragino Sans GB","Microsoft YaHei",sans-serif;
    background:
      radial-gradient(1200px 600px at 10% -10%, #13355e 0%, transparent 60%),
      radial-gradient(900px 500px at 110% 10%, #1b2e57 0%, transparent 55%),
      var(--bg);
    min-height:100vh;
  }
  .wrap{max-width:980px;margin:0 auto}
  header{display:flex;align-items:center;gap:14px;margin-bottom:22px}
  .logo{font-size:34px;filter:drop-shadow(0 4px 14px rgba(77,163,255,.5))}
  h1{font-size:22px;margin:0;letter-spacing:.5px}
  .sub{color:var(--dim);font-size:12px;margin-top:2px}
  .chip{margin-left:auto;font-size:12px;padding:6px 12px;border-radius:999px;
        border:1px solid var(--line);background:rgba(71,215,161,.1);color:var(--good)}
  .chip.off{color:var(--bad);background:rgba(255,122,144,.1)}
  .card{background:var(--card);border:1px solid var(--line);border-radius:16px;
        padding:18px 20px;margin-bottom:16px;backdrop-filter:blur(12px);
        box-shadow:0 10px 40px rgba(0,0,0,.25)}
  h2{font-size:14px;margin:0 0 12px;color:var(--accent2);font-weight:600;letter-spacing:.4px}
  .row{display:flex;align-items:center;gap:10px;margin:8px 0;flex-wrap:wrap}
  .row label{width:104px;color:var(--dim);font-size:12px;flex:none}
  code{font:12.5px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
       background:rgba(0,0,0,.35);border:1px solid var(--line);border-radius:8px;
       padding:7px 10px;color:#bfe0ff;word-break:break-all;flex:1;min-width:220px}
  button{cursor:pointer;font:inherit;font-size:12.5px;color:var(--text);
         background:rgba(77,163,255,.14);border:1px solid rgba(77,163,255,.35);
         padding:7px 14px;border-radius:9px;transition:.15s}
  button:hover{background:rgba(77,163,255,.26)}
  button.ghost{background:transparent;border-color:var(--line);color:var(--dim)}
  button.ghost:hover{color:var(--text);border-color:rgba(120,170,255,.4)}
  button.danger{background:rgba(255,122,144,.1);border-color:rgba(255,122,144,.3);color:#ffb3c0}
  button.danger:hover{background:rgba(255,122,144,.2)}
  .hint{color:var(--dim);font-size:12px;margin:10px 0 0}
  .drop{border:1.5px dashed rgba(120,170,255,.35);border-radius:14px;padding:26px;
        text-align:center;color:var(--dim);cursor:pointer;transition:.2s;
        background:rgba(77,163,255,.04)}
  .drop:hover,.drop.over{border-color:var(--accent);color:var(--text);background:rgba(77,163,255,.1)}
  .drop b{color:var(--accent2)}
  .queue{margin-top:12px;display:flex;flex-direction:column;gap:8px}
  .qrow{display:flex;align-items:center;gap:10px;font-size:12.5px}
  .qname{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .bar{width:150px;height:6px;border-radius:99px;background:rgba(255,255,255,.1);overflow:hidden;flex:none}
  .bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--accent),var(--accent2));transition:width .2s}
  .qstat{width:64px;text-align:right;color:var(--dim);flex:none}
  table{width:100%;border-collapse:collapse;font-size:13px}
  th{color:var(--dim);font-weight:500;font-size:11.5px;text-align:left;padding:6px 8px;
     border-bottom:1px solid var(--line);letter-spacing:.4px}
  td{padding:9px 8px;border-bottom:1px solid rgba(120,170,255,.07);vertical-align:middle}
  tr:last-child td{border-bottom:none}
  td.name{word-break:break-all}
  td.num{color:var(--dim);white-space:nowrap;font-variant-numeric:tabular-nums}
  td.ops{text-align:right;white-space:nowrap}
  td.ops button{padding:4px 10px;font-size:11.5px;margin-left:6px}
  .summary{color:var(--dim);font-weight:400;font-size:12px;margin-left:8px}
  .empty{color:var(--dim);text-align:center;padding:26px 0;font-size:13px}
  ul.acts{list-style:none;margin:0;padding:0;font-size:12px;color:var(--dim)}
  ul.acts li{display:flex;gap:10px;padding:4px 0;border-bottom:1px dashed rgba(120,170,255,.08)}
  ul.acts li:last-child{border:none}
  ul.acts .t{color:#6f88ad;font-variant-numeric:tabular-nums}
  ul.acts .p{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#b7cbe6}
  ul.acts .s{color:var(--good)}
  ul.acts .s.bad{color:var(--bad)}
  footer{color:#5b6f8f;font-size:11.5px;text-align:center;margin-top:26px}
  .toast{position:fixed;left:50%;bottom:28px;transform:translate(-50%,20px);opacity:0;
         background:#16233b;border:1px solid var(--line);color:var(--text);
         padding:10px 18px;border-radius:10px;font-size:13px;transition:.25s;pointer-events:none;
         box-shadow:0 10px 40px rgba(0,0,0,.4)}
  .toast.show{opacity:1;transform:translate(-50%,0)}
  .toast.bad{border-color:rgba(255,122,144,.4);color:#ffb3c0}
  .tabs{display:flex;gap:8px;margin-bottom:18px;flex-wrap:wrap}
  .tab{padding:9px 18px;border-radius:12px;border:1px solid var(--line);
       background:rgba(19,30,50,.7);color:var(--dim);cursor:pointer;font:inherit;font-size:13px;transition:.15s}
  .tab:hover{color:var(--text);border-color:rgba(120,170,255,.4)}
  .tab.active{color:#fff;border-color:rgba(77,163,255,.55);
              background:linear-gradient(180deg,rgba(77,163,255,.3),rgba(77,163,255,.12));
              box-shadow:0 6px 20px rgba(77,163,255,.18)}
  input.txt{background:rgba(0,0,0,.35);color:#bfe0ff;border:1px solid var(--line);
            border-radius:8px;padding:7px 10px;font:12.5px/1.5 ui-monospace,Menlo,monospace}
  input.txt:focus{outline:none;border-color:rgba(77,163,255,.6)}
  .crumb{display:flex;gap:2px;flex-wrap:wrap;align-items:center;font-size:13px;margin:4px 0 2px}
  .crumb a{color:var(--accent2);text-decoration:none;cursor:pointer}
  .crumb a:hover{text-decoration:underline}
  .crumb span{color:#5b6f8f;margin:0 3px}
  .chips{display:flex;gap:10px;flex-wrap:wrap}
  .pill{border:1px solid var(--line);border-radius:12px;padding:12px 14px;
        background:rgba(255,255,255,.03);flex:1;min-width:210px}
  .pill b{display:block;font-size:13px;margin-bottom:3px}
  .pill small{color:var(--dim);font-size:11.5px;line-height:1.5;display:block}
  .pill.on{border-color:rgba(71,215,161,.4);background:rgba(71,215,161,.07)}
  .pill.on b{color:var(--good)}
  .pill.off{opacity:.5}
  .quick{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0 0}
  .quick button{padding:5px 11px;font-size:11.5px}
  td.name a{color:var(--accent2);text-decoration:none;cursor:pointer}
  td.name a:hover{text-decoration:underline}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="logo">🐋</div>
    <div>
      <h1>爬爬的 Switch 游戏库</h1>
      <div class="sub">DBI Home server · 局域网直连安装</div>
    </div>
    <div class="chip" id="chip">连接中…</div>
  </header>

  <div class="tabs">
    <button class="tab active" data-tab="lib">🎮 游戏库</button>
    <button class="tab" data-tab="sd">🗂 Switch 文件</button>
  </div>

  <div id="panel-lib">

  <section class="card">
    <h2>① 在 DBI 里连过来</h2>
    <div class="row">
      <label>服务器地址</label>
      <code id="url">—</code>
      <button data-copy="url">复制</button>
    </div>
    <div class="row">
      <label>写进配置</label>
      <code id="cfg">—</code>
      <button data-copy="cfg">复制</button>
    </div>
    <div class="row">
      <label>本机地址</label>
      <select id="ipsel" style="background:rgba(0,0,0,.35);color:#bfe0ff;border:1px solid var(--line);border-radius:8px;padding:7px 10px;font:12.5px/1.5 ui-monospace,Menlo,monospace"></select>
    </div>
    <p class="hint">把上面第二行加到 SD 卡 <code style="flex:none">switch/DBI/dbi.config</code> 的 <b>[Network sources]</b> 下面；Switch 和 Mac 连同一个 Wi-Fi，DBI 主菜单就会出现「SwitchLibrary」。</p>
  </section>

  <section class="card">
    <h2>② 把游戏丢进来</h2>
    <div class="drop" id="drop">
      拖 NSP / XCI / NSZ 到这里，或 <b>点击选择文件</b>
      <input type="file" id="file" multiple hidden>
    </div>
    <div class="queue" id="queue"></div>
  </section>

  <section class="card">
    <h2>③ 库里的东西 <span class="summary" id="summary"></span></h2>
    <table>
      <thead><tr><th>文件名</th><th style="width:90px">大小</th><th style="width:130px">修改时间</th><th style="width:170px"></th></tr></thead>
      <tbody id="rows"></tbody>
    </table>
    <div class="empty" id="empty">还空着，先丢个 NSP 进来吧。</div>
  </section>

  <section class="card">
    <h2>最近请求</h2>
    <ul class="acts" id="acts"><li><span class="t">--:--</span><span class="p">还没有请求</span></li></ul>
  </section>
  </div>

  <div id="panel-sd" hidden>
    <section class="card">
      <h2>① 连上 Switch 的文件服务</h2>
      <div class="row">
        <label>Switch 地址</label>
        <input class="txt" id="swHost" placeholder="192.168.10.x" style="width:160px">
        <label style="width:auto">端口</label>
        <input class="txt" id="swPort" value="5000" style="width:86px">
        <button id="swConnect">连接</button>
        <span class="hint" id="swState" style="margin:0">未连接</span>
      </div>
      <p class="hint">Switch 上打开 <b>DBI → 主菜单 → 启动 FTP 服务器（Run FTP server）</b>，默认端口 5000。连上之后，这里能浏览、上传、下载、改名、删除 Switch SD 卡里的文件——插件（.ovl / .nro / sysmodule）直接丢进对应目录就行。</p>
    </section>

    <section class="card">
      <h2>② 存储分区</h2>
      <div class="chips">
        <div class="pill on" id="pill-sd"><b>SD 卡</b><small>可读写 · 走 DBI FTP :5000<br>游戏、插件、自制程序都在这里</small></div>
        <div class="pill off"><b>机身内存 USER / SYSTEM</b><small>只读 · 需要数据线 + DBI 的 MTP 模式<br>网页这边暂不支持</small></div>
        <div class="pill off"><b>游戏卡</b><small>插卡带后在 DBI 里直接安装<br>网页不参与</small></div>
      </div>
    </section>

    <section class="card">
      <h2>③ 浏览 SD 卡 <span class="summary" id="swWhere"></span></h2>
      <div class="crumb" id="swCrumb"><span>还没连接</span></div>
      <div class="quick" id="swQuick"></div>
      <div class="drop" id="swDrop" style="margin-top:14px">
        拖文件到这里，上传到当前目录，或 <b>点击选择文件</b>
        <input type="file" id="swFile" multiple hidden>
      </div>
      <div class="queue" id="swQueue"></div>
      <div class="row" style="margin-top:12px">
        <button class="ghost" id="swMkdir">新建文件夹</button>
        <button class="ghost" id="swRefresh">刷新</button>
      </div>
      <table style="margin-top:10px">
        <thead><tr><th>名字</th><th style="width:88px">大小</th><th style="width:140px">修改时间</th><th style="width:200px"></th></tr></thead>
        <tbody id="swRows"></tbody>
      </table>
      <div class="empty" id="swEmpty">先在上面把 Switch 的 IP 填上，点「连接」。</div>
    </section>
  </div>

  <footer>只给局域网用 · 别把端口映射到公网 · 库目录 <span id="dir">—</span></footer>
</div>
<div class="toast" id="toast"></div>

<script>
const $ = (s) => document.querySelector(s);
let ips = [];
let curIp = "";

function human(n){
  if(n === null || n === undefined) return "-";
  const u = ["B","KB","MB","GB","TB"]; let f = n, i = 0;
  while(f >= 1024 && i < u.length - 1){ f /= 1024; i++; }
  return (i === 0 ? f : f.toFixed(1)) + " " + u[i];
}
function ts(t){ const d = new Date(t * 1000); return d.toLocaleString(); }
function toast(msg, bad){
  const el = $("#toast"); el.textContent = msg;
  el.className = "toast show" + (bad ? " bad" : "");
  clearTimeout(el._t); el._t = setTimeout(() => el.className = "toast", 2200);
}
function copyText(text){
  navigator.clipboard.writeText(text).then(() => toast("已复制"), () => {
    const ta = document.createElement("textarea"); ta.value = text; document.body.appendChild(ta);
    ta.select(); document.execCommand("copy"); ta.remove(); toast("已复制");
  });
}
document.querySelectorAll("[data-copy]").forEach(btn => {
  btn.addEventListener("click", () => copyText($("#" + btn.dataset.copy).textContent));
});

function setIps(list){
  ips = list || [];
  if(!ips.length) return;
  if(!curIp || !ips.includes(curIp)) curIp = ips[0];
  const sel = $("#ipsel");
  sel.innerHTML = "";
  ips.forEach(ip => {
    const o = document.createElement("option"); o.value = ip; o.textContent = ip;
    if(ip === curIp) o.selected = true;
    sel.appendChild(o);
  });
  updateUrls();
}
function updateUrls(){
  const base = "http://" + (curIp || "127.0.0.1") + ":" + location.port;
  $("#url").textContent = base + "/files/";
  $("#cfg").textContent = "SwitchLibrary=ApacheHTTP|" + base + "/files/";
}
$("#ipsel").addEventListener("change", e => { curIp = e.target.value; updateUrls(); });

async function refresh(){
  try{
    const r = await fetch("/api/status", {cache: "no-store"});
    const s = await r.json();
    setIps(s.ips);
    $("#chip").textContent = "● 运行中 · " + s.count + " 个文件";
    $("#chip").className = "chip";
    $("#dir").textContent = s.dir;
    const total = s.size, free = s.free;
    $("#summary").textContent = "共 " + s.count + " 个 · " + human(total) +
      (free ? " · 磁盘剩余 " + human(free) : "");
    renderRows(s.files);
    renderActs(s.activity);
  }catch(e){
    $("#chip").textContent = "● 已断开";
    $("#chip").className = "chip off";
  }
}

function renderRows(files){
  const tb = $("#rows"); tb.innerHTML = "";
  $("#empty").style.display = files.length ? "none" : "block";
  files.forEach(f => {
    const tr = document.createElement("tr");
    const td1 = document.createElement("td"); td1.className = "name"; td1.textContent = f.name;
    const td2 = document.createElement("td"); td2.className = "num"; td2.textContent = human(f.size);
    const td3 = document.createElement("td"); td3.className = "num"; td3.textContent = ts(f.mtime);
    const td4 = document.createElement("td"); td4.className = "ops";
    const b1 = document.createElement("button"); b1.className = "ghost"; b1.textContent = "链接";
    b1.onclick = () => copyText("http://" + curIp + ":" + location.port + "/files/" + encodeURIComponent(f.name));
    const b2 = document.createElement("button"); b2.className = "ghost"; b2.textContent = "改名";
    b2.onclick = () => renameFile(f.name);
    const b3 = document.createElement("button"); b3.className = "danger"; b3.textContent = "删除";
    b3.onclick = () => deleteFile(f.name);
    td4.append(b1, b2, b3);
    tr.append(td1, td2, td3, td4); tb.appendChild(tr);
  });
}

function renderActs(acts){
  const ul = $("#acts"); ul.innerHTML = "";
  if(!acts || !acts.length){
    ul.innerHTML = '<li><span class="t">--:--</span><span class="p">还没有请求</span></li>';
    return;
  }
  acts.forEach(a => {
    const li = document.createElement("li");
    const t = document.createElement("span"); t.className = "t"; t.textContent = a.time;
    const p = document.createElement("span"); p.className = "p";
    p.textContent = a.method + " " + a.path + (a.ua ? "  · " + a.ua : "");
    const s = document.createElement("span");
    s.className = "s" + (a.status >= 400 ? " bad" : ""); s.textContent = a.status;
    li.append(t, p, s); ul.appendChild(li);
  });
}

async function deleteFile(name){
  if(!confirm("确定从库里删除？\n" + name)) return;
  const r = await fetch("/api/delete", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({name})});
  const j = await r.json();
  if(j.ok){ toast("已删除"); refresh(); } else { toast(j.error || "删除失败", true); }
}
async function renameFile(name){
  const to = prompt("新的文件名：", name);
  if(!to || to === name) return;
  const r = await fetch("/api/rename", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({from: name, to})});
  const j = await r.json();
  if(j.ok){ toast("已改名"); refresh(); } else { toast(j.error || "改名失败", true); }
}

// ---- 上传
const drop = $("#drop"), input = $("#file");
drop.addEventListener("click", () => input.click());
drop.addEventListener("dragover", e => { e.preventDefault(); drop.classList.add("over"); });
drop.addEventListener("dragleave", () => drop.classList.remove("over"));
drop.addEventListener("drop", e => {
  e.preventDefault(); drop.classList.remove("over");
  if(e.dataTransfer.files.length) uploadAll(e.dataTransfer.files);
});
input.addEventListener("change", () => { if(input.files.length) uploadAll(input.files); input.value = ""; });

function uploadAll(files){ Array.from(files).forEach(f => uploadOne(f)); }

function uploadOne(file){
  const row = document.createElement("div"); row.className = "qrow";
  const name = document.createElement("div"); name.className = "qname"; name.textContent = file.name;
  const bar = document.createElement("div"); bar.className = "bar"; const fill = document.createElement("i"); bar.appendChild(fill);
  const stat = document.createElement("div"); stat.className = "qstat"; stat.textContent = "0%";
  row.append(name, bar, stat); $("#queue").appendChild(row);

  const xhr = new XMLHttpRequest();
  xhr.open("PUT", "/api/upload?name=" + encodeURIComponent(file.name));
  xhr.upload.onprogress = e => {
    if(!e.lengthComputable) return;
    const pct = Math.round(e.loaded / e.total * 100);
    fill.style.width = pct + "%"; stat.textContent = pct + "%";
  };
  xhr.onload = () => {
    if(xhr.status >= 200 && xhr.status < 300){
      fill.style.width = "100%"; stat.textContent = "完成";
      toast("已上传 " + file.name); refresh();
      setTimeout(() => row.remove(), 4000);
    }else{
      stat.textContent = "失败"; stat.style.color = "#ffb3c0";
      try{ toast(JSON.parse(xhr.responseText).error || "上传失败", true); }catch(e){ toast("上传失败", true); }
      setTimeout(() => row.remove(), 8000);
    }
  };
  xhr.onerror = () => { stat.textContent = "断线"; stat.style.color = "#ffb3c0"; };
  xhr.send(file);
}

// ---- 标签页
function showTab(name){
  document.querySelectorAll(".tab").forEach(x => x.classList.toggle("active", x.dataset.tab === name));
  $("#panel-lib").hidden = name !== "lib";
  $("#panel-sd").hidden = name !== "sd";
}
document.querySelectorAll(".tab").forEach(t => {
  t.addEventListener("click", () => showTab(t.dataset.tab));
});
if(location.hash === "#sd") showTab("sd");

// ---- Switch SD 文件管理（DBI FTP）
let sw = {host: "", port: 5000, path: "/", connected: false};
const SW_QUICK = [["/", "根目录"], ["/switch", "switch"], ["/switch/.overlays", ".overlays"],
                  ["/atmosphere/contents", "contents"], ["/bootloader", "bootloader"],
                  ["/Nintendo", "Nintendo"]];
$("#swHost").value = localStorage.getItem("swHost") || "";
$("#swPort").value = localStorage.getItem("swPort") || "5000";
function swBase(){ return "host=" + encodeURIComponent(sw.host) + "&port=" + sw.port; }
function joinPath(base, name){ return base === "/" ? "/" + name : base.replace(/\/+$/, "") + "/" + name; }
function swErr(msg){ $("#swState").textContent = msg; $("#swState").style.color = "var(--bad)"; toast(msg, true); }

async function swConnect(){
  const host = $("#swHost").value.trim();
  const port = parseInt($("#swPort").value, 10) || 5000;
  if(!host){ toast("先填 Switch 的 IP", true); return; }
  sw.host = host; sw.port = port;
  $("#swState").textContent = "连接中…"; $("#swState").style.color = "var(--dim)";
  try{
    const r = await fetch("/api/sw/list?path=/&" + swBase(), {cache: "no-store"});
    const j = await r.json();
    if(!j.ok) throw new Error(j.error || "连接失败");
    sw.connected = true;
    localStorage.setItem("swHost", host); localStorage.setItem("swPort", String(port));
    $("#swState").textContent = "已连接 " + host + ":" + port;
    $("#swState").style.color = "var(--good)";
    toast("连上 Switch 了");
    renderSw(j);
  }catch(e){
    sw.connected = false; swErr("连接失败：" + (e.message || e));
  }
}

async function swGo(path){
  if(!sw.connected){ toast("先连接", true); return; }
  try{
    const r = await fetch("/api/sw/list?path=" + encodeURIComponent(path) + "&" + swBase(), {cache: "no-store"});
    const j = await r.json();
    if(!j.ok) throw new Error(j.error || "打不开");
    renderSw(j);
  }catch(e){ swErr(e.message || "打不开这个目录"); }
}

function renderSw(j){
  sw.path = j.path;
  $("#swWhere").textContent = (j.count || 0) + " 项";
  const cr = $("#swCrumb"); cr.innerHTML = "";
  const root = document.createElement("a"); root.textContent = "SD 卡";
  root.onclick = () => swGo("/"); cr.appendChild(root);
  let acc = "";
  sw.path.split("/").filter(Boolean).forEach(p => {
    acc += "/" + p;
    const sep = document.createElement("span"); sep.textContent = "/"; cr.appendChild(sep);
    const a = document.createElement("a"); a.textContent = p;
    const target = acc; a.onclick = () => swGo(target); cr.appendChild(a);
  });
  const q = $("#swQuick"); q.innerHTML = "";
  SW_QUICK.forEach(([p, label]) => {
    const b = document.createElement("button"); b.className = "ghost"; b.textContent = label;
    b.onclick = () => swGo(p); q.appendChild(b);
  });
  const tb = $("#swRows"); tb.innerHTML = "";
  $("#swEmpty").style.display = j.items.length ? "none" : "block";
  $("#swEmpty").textContent = j.items.length ? "" : "这个目录是空的。";
  j.items.forEach(it => {
    const tr = document.createElement("tr");
    const n = document.createElement("td"); n.className = "name";
    if(it.dir){
      const a = document.createElement("a"); a.textContent = "📁 " + it.name;
      a.onclick = () => swGo(joinPath(sw.path, it.name)); n.appendChild(a);
    }else{
      n.textContent = "📄 " + it.name;
    }
    const s = document.createElement("td"); s.className = "num";
    s.textContent = it.dir ? "目录" : human(it.size);
    const t = document.createElement("td"); t.className = "num";
    t.textContent = it.mtime ? ts(it.mtime) : "-";
    const ops = document.createElement("td"); ops.className = "ops";
    if(!it.dir){
      const d = document.createElement("button"); d.className = "ghost"; d.textContent = "下载";
      d.onclick = () => { location.href = "/api/sw/download?path=" +
        encodeURIComponent(joinPath(sw.path, it.name)) + "&" + swBase(); };
      ops.appendChild(d);
    }
    const rn = document.createElement("button"); rn.className = "ghost"; rn.textContent = "改名";
    rn.onclick = () => swRename(it); ops.appendChild(rn);
    const del = document.createElement("button"); del.className = "danger"; del.textContent = "删除";
    del.onclick = () => swDelete(it); ops.appendChild(del);
    tr.append(n, s, t, ops); tb.appendChild(tr);
  });
}

async function swRename(it){
  const to = prompt("新的名字：", it.name);
  if(!to || to === it.name) return;
  const r = await fetch("/api/sw/rename", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({host: sw.host, port: sw.port, path: sw.path, from: it.name, to})});
  const j = await r.json();
  if(j.ok){ toast("已改名"); swGo(sw.path); } else { toast(j.error || "改名失败", true); }
}

async function swDelete(it){
  if(!confirm("确定删除？\n" + it.name)) return;
  const r = await fetch("/api/sw/delete", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({host: sw.host, port: sw.port, path: sw.path, name: it.name, dir: it.dir})});
  const j = await r.json();
  if(j.ok){ toast("已删除"); swGo(sw.path); } else { toast(j.error || "删除失败", true); }
}

async function swMkdir(){
  if(!sw.connected){ toast("先连接", true); return; }
  const name = prompt("新文件夹的名字：", "");
  if(!name || !name.trim()) return;
  const r = await fetch("/api/sw/mkdir", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({host: sw.host, port: sw.port, path: sw.path, name: name.trim()})});
  const j = await r.json();
  if(j.ok){ toast("已创建"); swGo(sw.path); } else { toast(j.error || "创建失败", true); }
}

$("#swConnect").onclick = swConnect;
$("#swRefresh").onclick = () => { if(sw.connected) swGo(sw.path); else swConnect(); };
$("#swMkdir").onclick = swMkdir;
$("#swHost").addEventListener("keydown", e => { if(e.key === "Enter") swConnect(); });

const swDrop = $("#swDrop"), swFile = $("#swFile");
swDrop.addEventListener("click", () => swFile.click());
swDrop.addEventListener("dragover", e => { e.preventDefault(); swDrop.classList.add("over"); });
swDrop.addEventListener("dragleave", () => swDrop.classList.remove("over"));
swDrop.addEventListener("drop", e => {
  e.preventDefault(); swDrop.classList.remove("over");
  if(!sw.connected){ toast("先连接", true); return; }
  if(e.dataTransfer.files.length) swUploadAll(e.dataTransfer.files);
});
swFile.addEventListener("change", () => {
  if(!sw.connected){ toast("先连接", true); swFile.value = ""; return; }
  if(swFile.files.length) swUploadAll(swFile.files); swFile.value = "";
});

function swUploadAll(files){ Array.from(files).forEach(f => swUploadOne(f)); }

function swUploadOne(file){
  const row = document.createElement("div"); row.className = "qrow";
  const name = document.createElement("div"); name.className = "qname"; name.textContent = file.name;
  const bar = document.createElement("div"); bar.className = "bar"; const fill = document.createElement("i"); bar.appendChild(fill);
  const stat = document.createElement("div"); stat.className = "qstat"; stat.textContent = "0%";
  row.append(name, bar, stat); $("#swQueue").appendChild(row);

  const xhr = new XMLHttpRequest();
  xhr.open("PUT", "/api/sw/upload?path=" + encodeURIComponent(sw.path) + "&name=" +
           encodeURIComponent(file.name) + "&" + swBase());
  xhr.upload.onprogress = e => {
    if(!e.lengthComputable) return;
    const pct = Math.round(e.loaded / e.total * 100);
    fill.style.width = pct + "%"; stat.textContent = pct + "%";
  };
  xhr.onload = () => {
    if(xhr.status >= 200 && xhr.status < 300){
      fill.style.width = "100%"; stat.textContent = "完成";
      toast("已传到 Switch · " + file.name); swGo(sw.path);
      setTimeout(() => row.remove(), 4000);
    }else{
      stat.textContent = "失败"; stat.style.color = "#ffb3c0";
      try{ toast(JSON.parse(xhr.responseText).error || "上传失败", true); }catch(e){ toast("上传失败", true); }
      setTimeout(() => row.remove(), 8000);
    }
  };
  xhr.onerror = () => { stat.textContent = "断线"; stat.style.color = "#ffb3c0"; };
  xhr.send(file);
}

refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
"""


# ---------------------------------------------------------------- 启动

def main():
    global ROOT, PORT
    ap = argparse.ArgumentParser(description=APP + " —— DBI Home server 管理面板")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--dir", default=os.path.expanduser("~/SwitchGames/游戏库"))
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    ROOT = os.path.abspath(os.path.expanduser(args.dir))
    os.makedirs(ROOT, exist_ok=True)
    PORT = args.port

    httpd = None
    last_err = None
    for p in range(args.port, args.port + 12):
        try:
            httpd = ThreadingHTTPServer((args.host, p), Handler)
            PORT = p
            break
        except OSError as exc:
            last_err = exc
            continue
    if httpd is None:
        sys.stderr.write("端口都被占了：%r\n" % last_err)
        sys.exit(1)

    httpd.daemon_threads = True
    ips = all_ips()
    print("=" * 58)
    print("  %s" % APP)
    print("=" * 58)
    print("  库目录 : %s" % ROOT)
    for ip in ips:
        print("  面板   : http://%s:%d/" % (ip, PORT))
        print("  DBI    : http://%s:%d/files/" % (ip, PORT))
    print("  Ctrl+C 停止")
    print("=" * 58)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
