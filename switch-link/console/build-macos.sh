#!/usr/bin/env bash
# 用本机 devkitPro 编译 switch-link.nro（不需要 Docker、不需要 sudo、不需要 Rosetta）
#
# 前置：
#   1) ./install-toolchain.sh 已把工具链装到 ~/devkitpro
#   2) corresponding source 已解压，例如 ~/build/slsrc
#
# 用法：
#   ./build-macos.sh                      # 源码在 ~/build/slsrc
#   ./build-macos.sh /path/to/source-root # 指定源码根目录
set -euo pipefail

SRC="${1:-$HOME/build/slsrc}"
HERE="$(cd "$(dirname "$0")" && pwd)"

# shellcheck disable=SC1091
source "${DEVKITPRO_ENV:-$HOME/devkitpro/env.sh}"

[ -n "${DEVKITPRO:-}" ] || { echo "DEVKITPRO 未设置"; exit 1; }
[ -d "$DEVKITPRO/libnx" ] || { echo "找不到 $DEVKITPRO/libnx，先跑 install-toolchain.sh"; exit 1; }
[ -d "$SRC/switch-link" ] || { echo "源码根目录不对：$SRC（应包含 switch-link/、sphaira/、libnx/）"; exit 1; }
command -v ninja >/dev/null || export PATH="$DEVKITPRO/tools/bin:$PATH"

echo "== 工具链 =="
echo "  DEVKITPRO = $DEVKITPRO"
echo "  gcc       = $(aarch64-none-elf-gcc -dumpversion)"
echo "  ninja     = $(command -v ninja)"
echo "  源码      = $SRC"
echo

bash "$SRC/switch-link/build-offline.sh"

OUT="$HERE/switch-link-macos.nro"
cp "$SRC/switch-link.nro" "$OUT"

echo
echo "== 产物 =="
ls -l "$OUT"
shasum -a 256 "$OUT"
