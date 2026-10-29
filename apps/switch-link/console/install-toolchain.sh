#!/usr/bin/env bash
# 把 ~/Downloads 里的 devkitPro 包安装到 ~/devkitpro（不需要 sudo，不碰 /opt）
# 用法：./install-toolchain.sh [包所在目录，默认 ~/Downloads]
set -euo pipefail

ROOT="${DKP_ROOT:-$HOME/devkitpro}"
SRC="${1:-$HOME/Downloads}"
PACMAN="$ROOT/pacman/bin/pacman"
CONF="$ROOT/pacman/etc/pacman-local.conf"

[ -x "$PACMAN" ] || { echo "找不到 pacman：$PACMAN"; exit 1; }
[ -f "$CONF" ]   || { echo "找不到配置：$CONF"; exit 1; }

export DYLD_LIBRARY_PATH="$ROOT/pacman/lib"

shopt -s nullglob
files=()
for f in "$SRC"/*.pkg.tar.zst "$SRC"/*.pkg.tar.xz; do
  files+=("$f")
done
if [ ${#files[@]} -eq 0 ]; then
  echo "在 $SRC 没找到 .pkg.tar.zst / .pkg.tar.xz"
  exit 1
fi

echo "== 待安装 ${#files[@]} 个包，RootDir=$ROOT =="
for f in "${files[@]}"; do echo "   $(basename "$f")"; done

"$PACMAN" -U --noconfirm --overwrite '*' \
  --config "$CONF" \
  --root "$ROOT" \
  --dbpath "$ROOT/var/lib/pacman" \
  --cachedir "$ROOT/var/cache/pacman/pkg" \
  "${files[@]}"

echo
echo "== 安装结果 =="
"$PACMAN" -Q --config "$CONF" --root "$ROOT" --dbpath "$ROOT/var/lib/pacman" 2>/dev/null || true

echo
echo "== 环境变量 =="
cat <<EOF
export DEVKITPRO=$ROOT/opt/devkitpro
export DEVKITARM=\$DEVKITPRO/devkitARM
export PATH="\$DEVKITPRO/tools/bin:\$DEVKITPRO/devkitA64/bin:\$PATH"
EOF
