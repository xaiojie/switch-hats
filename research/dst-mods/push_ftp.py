#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 overlay 推到 Switch 的 FTP（Switch Link 自带的 FTP，端口 5000）。

Switch 的 IP 会变，所以默认自动找，不用手写地址：
  python3 push_ftp.py                 # 自动发现
  python3 push_ftp.py --remove        # 自动发现后卸载
  python3 push_ftp.py 192.168.1.75    # 知道地址时也可以直接给

装：上传本游戏 romfs，核对远端文件清单并逐文件回读校验；有残留文件则报错
删：把 /atmosphere/contents/010090100E334000 整个删掉
"""

import argparse
import ftplib
import hashlib
import sys
from pathlib import Path

import find_switch

TITLE = "010090100E334000"
ROOT = Path(__file__).resolve().parent / "overlay"


def ensure_dir(ftp, path):
    parts = [p for p in path.split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass


def upload_tree(ftp, local_root: Path, remote_root: str):
    for f in sorted(local_root.rglob("*")):
        rel = f.relative_to(local_root).as_posix()
        remote = remote_root.rstrip("/") + "/" + rel
        if f.is_dir():
            ensure_dir(ftp, remote)
        else:
            ensure_dir(ftp, remote.rsplit("/", 1)[0])
            with open(f, "rb") as fh:
                ftp.storbinary("STOR " + remote, fh)
            print("  ↑ %s (%d B)" % (rel, f.stat().st_size))
    verify_tree(ftp, local_root, remote_root)


def verify_tree(ftp, local_root: Path, remote_root: str):
    """Reject stale overrides and damaged transfers without deleting unknown files."""
    remote_files = set()

    def walk(path, prefix=""):
        for name, facts in ftp.mlsd(path):
            if name in (".", ".."):
                continue
            rel = prefix + name
            if facts.get("type") == "dir":
                walk(path + "/" + name, rel + "/")
            elif facts.get("type") == "file":
                remote_files.add(rel)
            else:
                raise RuntimeError("无法识别远端文件类型：" + rel)

    walk(remote_root.rstrip("/"))
    local_files = {p.relative_to(local_root).as_posix(): p
                   for p in local_root.rglob("*") if p.is_file()}
    extra = remote_files - local_files.keys()
    missing = local_files.keys() - remote_files
    if extra or missing:
        raise RuntimeError("远端覆盖层不一致；残留文件：%s；缺失文件：%s。请先备份并清理残留，再重推。"
                           % (sorted(extra), sorted(missing)))
    for rel, path in sorted(local_files.items()):
        remote_hash = hashlib.sha256()
        ftp.retrbinary("RETR " + remote_root.rstrip("/") + "/" + rel, remote_hash.update)
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        if remote_hash.hexdigest() != expected:
            raise RuntimeError("上传回读校验失败：" + rel)
    print("  ✓ 远端文件清单一致，%d 个文件 SHA256 回读校验通过" % len(local_files))


def remove_tree(ftp, path):
    try:
        entries = list(ftp.mlsd(path))
    except ftplib.error_perm as exc:
        if "no such file" in str(exc).lower() or "not found" in str(exc).lower():
            print("不存在：%s" % path)
            return
        raise
    for name, facts in entries:
        if name in (".", ".."):
            continue
        child = path.rstrip("/") + "/" + name
        if facts.get("type") == "dir":
            remove_tree(ftp, child)
        elif facts.get("type") == "file":
            ftp.delete(child)
            print("  ✗ %s" % child)
        else:
            raise RuntimeError("无法识别远端文件类型：" + child)
    ftp.rmd(path)
    print("  ✗ %s/" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", help="Switch 的 IP；省略则自动发现")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--remove", action="store_true", help="改成卸载")
    args = ap.parse_args()

    try:
        host, port, source = find_switch.discover(args.host, args.port or find_switch.DEFAULT_PORT)
    except RuntimeError as exc:
        sys.exit(str(exc))
    print("目标 %s:%d（来源：%s）" % (host, port, source))
    find_switch.save_cache(host, port)

    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=20)
    ftp.login()
    print("已连接 %s:%d" % (host, port))

    if args.remove:
        remove_tree(ftp, "/atmosphere/contents/" + TITLE)
    else:
        base = ROOT / "atmosphere" / "contents" / TITLE / "romfs"
        if not base.is_dir():
            sys.exit("找不到 %s，先跑 build_overlay.py" % base)
        upload_tree(ftp, base, "/atmosphere/contents/" + TITLE + "/romfs")

    ftp.quit()
    print("完成")


if __name__ == "__main__":
    main()
