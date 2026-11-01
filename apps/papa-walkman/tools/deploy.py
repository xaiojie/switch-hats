#!/usr/bin/env python3
"""一键部署 papa-walkman 到 Switch 并取回日志。

用法：
  python3 tools/deploy.py            # 上传 build/papa-walkman.nro + 回读校验
  python3 tools/deploy.py --logs     # 只取日志
  python3 tools/deploy.py --push --logs

FTP: 192.168.10.3:5000, anonymous, 被动模式。
"""
import argparse
import hashlib
import io
import os
import sys

import ftplib

HOST = os.environ.get("SWITCH_FTP_HOST", "192.168.10.3")
PORT = int(os.environ.get("SWITCH_FTP_PORT", "5000"))
NRO_LOCAL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "build", "papa-walkman.nro")
NRO_REMOTE = "/sdmc:/switch/papa-walkman/papa-walkman.nro"
LOGS = ["/sdmc:/config/papa-walkman/log.txt", "/sdmc:/config/papa-walkman/trace.txt"]


def connect():
    ftp = ftplib.FTP()
    ftp.connect(HOST, PORT, timeout=30)
    ftp.login("anonymous", "")
    ftp.set_pasv(True)
    return ftp


def push(ftp):
    data = open(NRO_LOCAL, "rb").read()
    local = hashlib.sha256(data).hexdigest()
    ftp.storbinary("STOR " + NRO_REMOTE, io.BytesIO(data), blocksize=32768)
    back = io.BytesIO()
    ftp.retrbinary("RETR " + NRO_REMOTE, back.write)
    remote = hashlib.sha256(back.getvalue()).hexdigest()
    print(f"nro {len(data)} bytes  local={local}  remote={remote}  "
          f"{'OK' if local == remote else 'MISMATCH'}")
    return local == remote


def logs(ftp, clear=False):
    for path in LOGS:
        buf = io.BytesIO()
        try:
            ftp.retrbinary("RETR " + path, buf.write)
        except ftplib.error_perm as exc:
            print(f"== {path}: {exc}")
            continue
        text = buf.getvalue().decode("utf-8", "replace")
        print(f"== {path} ({len(text)} chars)\n{text}")
        if clear:
            ftp.storbinary("STOR " + path, io.BytesIO())
            check = io.BytesIO()
            ftp.retrbinary("RETR " + path, check.write)
            if check.getvalue():
                raise RuntimeError(f"Log clear verification failed: {path}")
            print(f"cleared and read back empty: {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--push", action="store_true")
    ap.add_argument("--logs", action="store_true")
    ap.add_argument("--clear", action="store_true")
    args = ap.parse_args()
    if not (args.push or args.logs):
        args.push = True
        args.logs = True
    ftp = connect()
    try:
        if args.push and not push(ftp):
            return 1
        if args.logs:
            logs(ftp, args.clear)
    finally:
        try:
            ftp.quit()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
