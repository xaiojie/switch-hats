#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# 本机装了 devkitPro 时（见 docs/dev-environment.md）自动带上工具链环境
if [ -z "${DEVKITPRO:-}" ] && [ -f "$HOME/devkitpro/env.sh" ]; then
  # shellcheck disable=SC1090
  . "$HOME/devkitpro/env.sh"
fi
: "${DEVKITPRO:?Set DEVKITPRO to your installed devkitPro directory (see docs/dev-environment.md)}"

export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "${JOBS:-4}"
echo "Built build/papa-walkman.nro"
