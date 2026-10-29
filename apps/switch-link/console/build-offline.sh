#!/usr/bin/env bash
set -eu
# From the corresponding-source archive root, after installing devkitA64 +
# deko3d switch-cmake switch-curl switch-glm switch-zlib switch-mbedtls.
# Build bundled libnx first; only the configured DEVKITPRO directory is written.
: "${DEVKITPRO:?Set DEVKITPRO to your local devkitPro directory}"
root=$(cd "$(dirname "$0")/.." && pwd)
make -C "$root/libnx/nx" -j6 lib/libnx.a
mkdir -p "$DEVKITPRO/libnx/include" "$DEVKITPRO/libnx/lib"
cp -R "$root/libnx/nx/include/." "$DEVKITPRO/libnx/include/"
cp -R "$root/libnx/nx/external/bsd/include/." "$DEVKITPRO/libnx/include/"
cp "$root/libnx/nx/lib/libnx.a" "$DEVKITPRO/libnx/lib/"
cp "$root/libnx/nx/switch.ld" "$root/libnx/nx/switch.specs" "$root/libnx/nx/switch_rules" "$DEVKITPRO/libnx/"
dep_args=()
for name in ftpsrv libpulsar nanovg stb_nanovg stb minini yyjson zstd libnxtc dr_libs; do
    upper=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')
    dep_args+=("-DFETCHCONTENT_SOURCE_DIR_${upper}=$root/third_party/$name")
done
cmake -S "$root/sphaira" -B "$root/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DLTO=OFF -DENABLE_FTPSRV=ON \
    -DENABLE_NSZ=OFF -DENABLE_LIBUSBHSFS=OFF -DENABLE_NTFS=OFF \
    -DENABLE_LIBUSBDVD=OFF -DENABLE_LIBHAZE=OFF \
    -DENABLE_AUDIO_MP3=OFF -DENABLE_AUDIO_OGG=OFF -DENABLE_AUDIO_WAV=OFF -DENABLE_AUDIO_FLAC=OFF \
    -DENABLE_DEVOPTAB_NFS=OFF -DENABLE_DEVOPTAB_SMB2=OFF -DENABLE_DEVOPTAB_FTP=OFF \
    -DENABLE_DEVOPTAB_SFTP=OFF -DENABLE_DEVOPTAB_WEBDAV=OFF "${dep_args[@]}"
cmake --build "$root/build" --parallel 6
cp "$root/build/sphaira.nro" "$root/switch-link.nro"
