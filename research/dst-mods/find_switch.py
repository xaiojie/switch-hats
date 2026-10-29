#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""自动找到 Switch 的 FTP 地址。

Switch 的 IP 会变（DHCP），所以任何脚本、文档都不该写死地址。
本模块按下面的顺序去找，第一个真正握手成功的就用：

  1. --host / 环境变量 DST_SWITCH_HOST（显式指定，最高优先级）
  2. Switch Link 管理接口 http://127.0.0.1:8088/api/status 里的 device.ip
     （Switch 客户端连上 Mac 后，这里就是它当前的地址）
  3. 上次成功的地址缓存 ~/.cache/dst-switch-host
  4. 本机 ARP 邻居表里出现的地址
  5. 扫本机所在网段（按 en0 的掩码，最多 /22），找 5000 端口回 "220" 的主机

命令行用法：
  python3 find_switch.py            # 打印 "192.168.1.75 5000"
  python3 find_switch.py --host     # 只打印 IP
  python3 find_switch.py --json     # 打印 JSON，含来源
"""

import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

DEFAULT_PORT = 5000
ADMIN_STATUS = "http://127.0.0.1:8088/api/status"
CACHE = Path.home() / ".cache" / "dst-switch-host"


def probe(host, port=DEFAULT_PORT, timeout=1.5):
    """TCP 连上并且第一行是 FTP 220 欢迎语，才算真的能用。"""
    try:
        sock = socket.create_connection((host, port), timeout)
    except OSError:
        return False
    try:
        sock.settimeout(timeout)
        banner = sock.recv(128)
        return banner.startswith(b"220")
    except OSError:
        return False
    finally:
        sock.close()


# ---------- 来源 2：Switch Link 管理接口 ----------

def from_admin_api():
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(ADMIN_STATUS, timeout=2) as resp:
        data = json.load(resp)
    device = data.get("device") or {}
    if device.get("connected") and device.get("ip"):
        return device["ip"], int(device.get("port") or DEFAULT_PORT)
    return None


# ---------- 来源 3：缓存 ----------

def from_cache():
    try:
        parts = CACHE.read_text().split()
        if len(parts) == 2:
            return parts[0], int(parts[1])
    except (OSError, ValueError):
        pass
    return None


def save_cache(host, port):
    try:
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        CACHE.write_text("%s %s\n" % (host, port))
    except OSError:
        pass


# ---------- 来源 4：ARP 邻居 ----------

def from_arp():
    try:
        out = subprocess.run(["arp", "-an"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    return re.findall(r"\((\d+\.\d+\.\d+\.\d+)\) at", out)


# ---------- 来源 5：扫网段 ----------

def lan_hosts():
    """返回本机 en0 同网段里需要探测的地址（不含自己）。"""
    try:
        ip = subprocess.run(["ipconfig", "getifaddr", "en0"],
                            capture_output=True, text=True, timeout=5).stdout.strip()
        ifconfig = subprocess.run(["ifconfig", "en0"],
                                  capture_output=True, text=True, timeout=5).stdout
        mask = re.search(r"netmask (0x[0-9a-fA-F]+)", ifconfig)
        if not ip or not mask:
            return []
        netmask = struct.unpack("!I", struct.pack("!I", int(mask.group(1), 16)))[0]
    except (OSError, subprocess.SubprocessError):
        return []

    ip_i = struct.unpack("!I", socket.inet_aton(ip))[0]
    network = ip_i & netmask
    size = (~netmask) & 0xFFFFFFFF
    if size > 4096:                    # 太大就只扫 /24
        network = ip_i & 0xFFFFFF00
        size = 255
    return [socket.inet_ntoa(struct.pack("!I", network + i)) for i in range(1, size)
            if network + i != ip_i]


def sweep(port=DEFAULT_PORT, workers=128, timeout=0.6):
    hosts = lan_hosts()
    if not hosts:
        return []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(lambda h: h if probe(h, port, timeout) else None, hosts))
    return [h for h in results if h]


# ---------- 对外入口 ----------

def discover(explicit=None, port=DEFAULT_PORT, verbose=False):
    """返回 (host, port, source)；找不到抛 RuntimeError。"""
    def say(msg):
        if verbose:
            print(msg, file=sys.stderr)

    if explicit:
        if probe(explicit, port):
            return explicit, port, "参数"
        raise RuntimeError("指定的地址连不上：" + explicit)

    env = os.environ.get("DST_SWITCH_HOST")
    if env:
        say("试环境变量：%s" % env)
        if probe(env, port):
            return env, port, "环境变量"

    try:
        hit = from_admin_api()
        if hit:
            say("试 Switch Link 接口：%s:%d" % hit)
            if probe(hit[0], hit[1]):
                return hit[0], hit[1], "Switch Link 接口"
    except Exception as exc:            # 接口没起、返回异常都不算错
        say("Switch Link 接口不可用：%s" % exc)

    hit = from_cache()
    if hit:
        say("试缓存：%s:%d" % hit)
        if probe(hit[0], hit[1]):
            return hit[0], hit[1], "缓存"

    for host in from_arp():
        say("试 ARP 邻居：%s" % host)
        if probe(host, port):
            return host, port, "ARP 邻居"

    say("扫网段…")
    for host in sweep(port):
        return host, port, "网段扫描"

    raise RuntimeError(
        "没找到 Switch 的 FTP。确认：Switch 上 Switch Link 的 FTP 已开启、"
        "Mac 和 Switch 在同一 Wi-Fi、端口 %d 没被占用。" % port)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", action="store_true", help="只打印 IP（方便管道用）")
    ap.add_argument("--json", action="store_true", help="打印 JSON，含发现来源")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = ap.parse_args()
    try:
        host, port, source = discover(port=args.port, verbose=True)
    except RuntimeError as exc:
        sys.exit(str(exc))
    save_cache(host, port)
    if args.json:
        print(json.dumps({"host": host, "port": port, "source": source}, ensure_ascii=False))
    elif args.host:
        print(host)
    else:
        print("%s %d" % (host, port))


if __name__ == "__main__":
    main()
