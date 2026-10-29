#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 AI 生成的素材写进 HATS 整合包：
- 4 个启动项图标 -> bootloader/res/*.bmp (192x192, 32bpp)
- Hekate 开机壁纸 -> bootloader/bootlogo.bmp (720x1280, 32bpp, 横版逆时针转 90°)
- 大气层启动图 -> atmosphere/package3 (1280x720 PNG, 官方脚本打补丁)
原文件都会先备份 .orig。
"""
import os, shutil, struct, subprocess, sys
from PIL import Image, ImageOps

PACK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT = os.path.join(PACK, "_素材")
BUILD = os.path.join(PACK, "_build")

ICONS = {
    "ofw.bmp": "icon_ofw.png",
    "stock.bmp": "icon_stock.png",
    "sysmmc.bmp": "icon_sysmmc.png",
    "emummc.bmp": "icon_emummc.png",
}


def backup(path):
    b = path + ".orig"
    if os.path.exists(path) and not os.path.exists(b):
        shutil.copy2(path, b)
        print("backup", b)


def make_icon(src, dst):
    im = Image.open(src).convert("RGBA").resize((192, 192), Image.LANCZOS)
    im.save(dst, "BMP")
    b = open(dst, "rb").read(54)
    w, h = struct.unpack("<ii", b[18:26])
    bpp = struct.unpack("<H", b[28:30])[0]
    assert (w, h, bpp) == (192, 192, 32), (dst, w, h, bpp)
    print("icon", os.path.basename(dst), f"{w}x{h} {bpp}bpp", os.path.getsize(dst), "B")


def make_bootlogo(src, dst):
    im = Image.open(src).convert("RGBA")
    im = ImageOps.fit(im, (1280, 720), Image.LANCZOS).rotate(90, expand=True)
    assert im.size == (720, 1280), im.size
    im.save(dst, "BMP")
    b = open(dst, "rb").read(54)
    w, h = struct.unpack("<ii", b[18:26])
    bpp = struct.unpack("<H", b[28:30])[0]
    assert (w, h, bpp) == (720, 1280, 32), (dst, w, h, bpp)
    print("bootlogo", os.path.basename(dst), f"{w}x{h} {bpp}bpp", os.path.getsize(dst), "B")


def make_splash(src, png):
    im = Image.open(src).convert("RGB")
    im = ImageOps.fit(im, (1280, 720), Image.LANCZOS)
    im.save(png)
    print("splash png", png, im.size)


def patch_splash(png):
    pkg = os.path.join(PACK, "atmosphere", "package3")
    backup(pkg)
    script = os.path.join(BUILD, "insert_splash_screen.py")
    subprocess.run([sys.executable, script, png, pkg], check=True)
    b = open(pkg, "rb").read(4)
    size = os.path.getsize(pkg)
    assert b == b"PK31" and size == 0x800000, (b, size)
    print("package3 patched", size, "B PK31")


def main():
    os.makedirs(MAT, exist_ok=True)
    for name, src in ICONS.items():
        dst = os.path.join(PACK, "bootloader", "res", name)
        backup(dst)
        make_icon(os.path.join(MAT, src), dst)
    blog = os.path.join(PACK, "bootloader", "bootlogo.bmp")
    make_bootlogo(os.path.join(MAT, "bootlogo.png"), blog)
    png = os.path.join(BUILD, "splash_final.png")
    make_splash(os.path.join(MAT, "splash.png"), png)
    patch_splash(png)


if __name__ == "__main__":
    main()
