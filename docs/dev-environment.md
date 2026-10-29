# 本机开发环境（编译 Switch NRO 不用 Docker）

目标：在 Apple Silicon Mac 上**原生 arm64** 编出 `papa-walkman.nro` 和 `switch-link.nro`，不装 Docker、不要 sudo、不跑 Rosetta。

## 一、装了什么

全部装在 `~/devkitpro`（用户目录，可随时删）：

| 组件 | 版本 | 位置 |
|---|---|---|
| devkitA64 (gcc) | 16.1.0 | `~/devkitpro/devkitA64/bin` |
| binutils | 2.46.0 | 同上 |
| newlib | 4.6.0.20260123 | `~/devkitpro/devkitA64/aarch64-none-elf` |
| libnx | 4.12.0 | `~/devkitpro/libnx` |
| deko3d | 0.5.0 | `~/devkitpro/libnx`/`lib` |
| switch-cmake | 1.5.2 | `~/devkitpro/cmake` |
| switch-tools / uam | 1.13.1 / 1.1.0 | `~/devkitpro/tools/bin`（`elf2nro`、`nxlink`） |
| portlibs | libcurl 7.69.1、mbedtls 2.28.10、zlib 1.3.1 | `~/devkitpro/portlibs/switch` |
| pacman（可选） | 6.0.2 | `~/devkitpro/pacman`，非 root 不能装包，只当索引/校验用 |
| CMake + Ninja | 系统 brew 版 / 1.11.1 | `cmake`、`ninja` |

环境脚本 `~/devkitpro/env.sh` 导出 `DEVKITPRO`、`PATH`、`CC/CXX/AR/AS/RANLIB/LD/STRIP/OBJCOPY`。
**`build.sh` 会自动 source 它**，所以正常构建只要 `cd apps/xxx && ./build.sh`。

## 二、从零重装

```bash
# 1. 拿包（见下面「403 坑」；也可以直接用浏览器点开 URL 下载）
#    需要 19 个包，URL 清单在 apps/switch-link/console/fetch-toolchain-urls.txt
mkdir -p ~/Downloads/dkp-pkgs && cd ~/Downloads/dkp-pkgs
# 2. 解包到 ~/devkitpro（这些 .pkg.tar.zst 没有安装脚本，等价于 pacman -U）
for p in *.pkg.tar.zst; do tar --zstd -xf "$p" -C "$HOME/devkitpro"; done
# 3. 生成环境脚本
bash apps/switch-link/console/install-toolchain.sh
source ~/devkitpro/env.sh
```

`install-toolchain.sh` 只做「解包 + 写 env.sh + 自检」，不联网、不要 root。

## 三、构建

```bash
# 爬爬的随身听（CMake + Ninja）
cd apps/papa-walkman && ./build.sh            # -> build/papa-walkman.nro

# Switch Link 主机端（上游 build-offline.sh，源码全随包）
cd apps/switch-link/console && ./build-macos.sh   # -> console/switch-link-macos.nro
```

验证过的产物字节数（用来判断构建是否正常）：

| 产物 | 大小 |
|---|---|
| `apps/papa-walkman/build/papa-walkman.nro` | 1,918,245 B |
| `apps/switch-link/console/switch-link-macos.nro` | 4,035,785 B（与发布版一致） |

## 四、测试

```bash
cd apps/switch-link && python3 -m unittest discover -s tests     # 89 用例
```

需要 `pyftpdlib`（FTP 用例），装依赖走代理：`python3 -m pip install --proxy http://127.0.0.1:7897 --user pyftpdlib`。
当前已知：89 个用例里 **1 个失败是 Windows 专属**（`tests/test_music.py` 的 `test_windows_locked_orphan_...`，模拟 Windows 文件占用），macOS 上不适用。

## 五、坑（都踩过）

| 现象 | 原因 | 解法 |
|---|---|---|
| `pkg.devkitpro.org` 返回 **403 Cloudflare** | 该站按 TLS/HTTP2 **客户端指纹**拦非浏览器，`curl`/`python urllib` 一律 403，**Safari / URLSession 是 200** | 用浏览器下载，或写 Swift `URLSession` 下载器（`/tmp/dlpkgs.swift` 就是干这个的）。不要以为是「被墙」，换代理节点没用 |
| `aarch64-none-elf-gcc: command not found` | `switchvars.sh` 只把 `tools/bin` 加进 PATH | 手动 `PATH=$DEVKITPRO/devkitA64/bin:$PATH`（`env.sh` 已处理） |
| 链接报 `ld: unknown options: -specs=...` / `unrecognised emulation mode` | devkitPro 的 `switch_rules` 不导出 `LD`，make 退回系统 `/usr/bin/ld` | `export LD=aarch64-none-elf-g++`（`env.sh` 已处理） |
| `CMakeCache.txt is different than the directory` | 目录被整体移动过，`build/` 里是旧绝对路径 | `rm -rf build` 重新配置 |
| `internal compiler error: Segmentation fault`（老路，Docker） | arm64 镜像里 `cc1plus` 在模拟下崩 | 用本机原生工具链；若必须用镜像就 `docker run --platform linux/amd64` |
| `DEVKITPRO: Set DEVKITPRO ...` | 没 source 环境脚本且 `~/devkitpro/env.sh` 不存在 | 按第二节重装 |

## 六、为什么不走 Docker 了

早期用 `devkitpro/devkita64:latest`（2.72 GB）在 QEMU 下构建，问题多：磁盘被写满导致 Docker 守护进程 `metadata.db: input/output error`、必须 `--platform linux/amd64`、每次构建几十秒冷启动。本机工具链装好后构建只要几秒，镜像已删掉（释放 5.5 GB）。

## 七、版本一致性

`apps/papa-walkman/toolchain-lock.json` 记录了构建用的包版本/哈希。换机器时用它对比：

```bash
python3 - <<'PY'
import json, pathlib
lock = json.loads(pathlib.Path('apps/papa-walkman/toolchain-lock.json').read_text())
print(len(lock.get('packages', lock) if isinstance(lock, dict) else lock))
PY
```

已知差异：锁文件里 4 条记的是 `windows/x86_64` 包，本机装的是 `osx/arm64` 包，同名 6 条完全一致。
