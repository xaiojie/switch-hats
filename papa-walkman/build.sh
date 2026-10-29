#!/usr/bin/env bash
# 在 devkitpro/devkita64 容器里构建 NRO。
#
# 不需要装任何 portlibs：解码用仓库内自带的 minimp3，音频走 libnx 的 audout，
# 所以镜像里现成的 devkitA64 + libnx 就够。
#
# 用法：./build.sh
set -euo pipefail

cd "$(dirname "$0")"
ROOT="$(pwd)"
IMAGE="devkitpro/devkita64:latest"
# 必须用 amd64 变体：arm64 镜像里的 devkitA64 gcc 15.2.0 的 cc1plus 在
# Docker Desktop 的 aarch64 VM 里一编译就 ICE 段错误（连 3 行 hello.cpp 都挂）。
PLATFORM="linux/amd64"

echo "==> 工程目录: $ROOT"

docker run --rm --platform "$PLATFORM" -v "$ROOT":/work -w /work "$IMAGE" bash -lc '
  set -euo pipefail
  export DEVKITPRO=/opt/devkitpro
  # switchvars.sh 会导出 CC/CXX/LD（指向 devkitA64 的绝对路径），
  # 但它的 PATH 里没有 devkitA64/bin，所以再补一条。
  . /opt/devkitpro/switchvars.sh
  export PATH="/opt/devkitpro/devkitA64/bin:$PATH"
  # devkita64.sh 不设 LD，make 会退回系统自带的 /usr/bin/ld（x86_64）
  export LD=aarch64-none-elf-g++

  echo "==> toolchain"
  aarch64-none-elf-gcc --version | head -1
  [ -d /opt/devkitpro/libnx ] && echo "==> libnx: ok" || { echo "!! libnx 缺失"; exit 1; }

  echo "==> 清理旧产物"
  make clean >/dev/null 2>&1 || true

  echo "==> 编译"
  make -j"$(nproc)"
'

echo "==> 产物"
ls -lh "$ROOT"/*.nro 2>/dev/null || { echo "没有找到 .nro，看上面的错误"; exit 1; }
