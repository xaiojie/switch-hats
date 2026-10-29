#!/bin/bash
# 一键写卡：把整合包拷到新 SD 卡里（只复制，不格式化、不删除卡里已存在的其它东西）
# 用法：插好卡 → 双击本文件 → 按提示选卡 → 输入 y 确认

set -uo pipefail

SRC="/Users/jay/Downloads/Switch-HATS-2026-10-16-中文Hekate"

pause_exit() {
  echo
  read -r -p "按回车关闭窗口…" _ || true
  exit "${1:-0}"
}

echo "======================================"
echo "   爬爬的 Switch 整合包 · 一键写卡"
echo "======================================"
echo

if [ ! -d "$SRC" ]; then
  echo "❌ 找不到源文件夹："
  echo "   $SRC"
  pause_exit 1
fi

# 列出可写的外置卷
vols=()
for v in /Volumes/*; do
  [ -d "$v" ] || continue
  base="$(basename "$v")"
  case "$base" in
    Macintosh*|Recovery|.*) continue ;;
  esac
  vols+=("$v")
done

if [ ${#vols[@]} -eq 0 ]; then
  echo "❌ 没检测到外置磁盘。"
  echo "   请先插好 SD 卡（读卡器），等它出现在访达侧边栏，再运行本脚本。"
  pause_exit 1
fi

echo "检测到这些外置卷："
i=1
for v in "${vols[@]}"; do
  fs="$(diskutil info "$v" 2>/dev/null | awk -F': *' '/File System Personality/{print $2; exit}')"
  free="$(df -h "$v" 2>/dev/null | awk 'NR==2{print $4}')"
  echo "  $i) $v    [${fs:-未知}]  可用 ${free:-?}"
  i=$((i+1))
done
echo

read -r -p "选哪个？输入编号（直接回车取消）： " pick || true
[ -z "${pick:-}" ] && { echo "已取消。"; pause_exit 0; }

case "$pick" in
  ''|*[!0-9]*) echo "❌ 请输入数字编号。"; pause_exit 1 ;;
esac
if [ "$pick" -lt 1 ] || [ "$pick" -gt ${#vols[@]} ]; then
  echo "❌ 编号超出范围。"
  pause_exit 1
fi
TARGET="${vols[$((pick-1))]}"

echo
echo "源目录：$SRC"
echo "目标卡：$TARGET"
echo
echo "会做的事：把整合包复制进这张卡，并清掉 macOS 生成的 ._ 垃圾文件。"
echo "不会做的事：不格式化、不删卡里其它文件。"
echo
read -r -p "确认写入？输入 y 继续： " ok || true
if [ "${ok:-}" != "y" ] && [ "${ok:-}" != "Y" ]; then
  echo "已取消。"
  pause_exit 0
fi

echo
echo "开始复制（284 个文件，大约 1～3 分钟）…"
rsync -a --no-owner --no-perms \
  --exclude '.Spotlight-V100' --exclude '.fseventsd' --exclude '.Trashes' \
  --exclude '._*' --exclude '.DS_Store' \
  "$SRC/" "$TARGET/"
rc=$?
if [ $rc -ne 0 ]; then
  echo "❌ 复制出错（rsync 退出码 $rc）。可能是卡写保护或空间不足。"
  pause_exit 1
fi

# 清理 AppleDouble
find "$TARGET" -name '._*' -type f -delete 2>/dev/null
find "$TARGET" -name '.DS_Store' -type f -delete 2>/dev/null
touch "$TARGET/.metadata_never_index" 2>/dev/null
sync

echo
echo "校验关键文件…"
bad=0
for f in payload.bin bootloader/sys/nyx.bin atmosphere/package3 hbmenu.nro bootloader/hekate_ipl.ini; do
  if [ ! -f "$TARGET/$f" ]; then
    echo "  ❌ 缺少 $f"
    bad=1
    continue
  fi
  a="$(shasum -a 256 "$SRC/$f" 2>/dev/null | awk '{print $1}')"
  b="$(shasum -a 256 "$TARGET/$f" 2>/dev/null | awk '{print $1}')"
  if [ "$a" = "$b" ] && [ -n "$a" ]; then
    echo "  ✅ $f"
  else
    echo "  ❌ $f 哈希不一致"
    bad=1
  fi
done

echo
if [ $bad -eq 0 ]; then
  echo "🎉 写卡完成！可以拔卡插回 Switch 了。"
  echo "   第一次开机进 Hekate → emuMMC → Create emuMMC → SD File 建虚拟系统。"
else
  echo "⚠️ 有文件没对上，建议重新跑一次本脚本。"
fi
open "$TARGET" 2>/dev/null || true
pause_exit 0
