#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${DEVKITPRO:?Set DEVKITPRO to your installed devkitPro directory}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "${JOBS:-4}"
echo "Built build/papa-walkman.nro"
