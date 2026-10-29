# 爬爬的随身听

Switch 上的本地音乐播放器。外观是一台会转的磁带机。

- 设计文档：[docs/DESIGN.md](docs/DESIGN.md)
- 状态：**M1（出声）已编译、已装到机器**，等实测

## 目录

```
papa-walkman/
├── Makefile                devkitPro 经典 Makefile 模板（libnx/switch_rules）
├── build.sh                Docker 里跑 make 的一键脚本
├── source/main.cpp         M1：扫 /music、minimp3 解码、libnx audout 出声
├── third_party/minimp3/    随仓库分发的 MP3 解码器（不依赖 portlibs）
└── docs/DESIGN.md          功能设计 v0.1
```

## 构建

```bash
./build.sh
```

产物：`papa-walkman.nro`（约 284 KB）。

脚本做的事：

1. `docker run --platform linux/amd64 devkitpro/devkita64:latest`
2. 容器内 `. /opt/devkitpro/switchvars.sh`（拿到 CC/CXX），再补 `LD=aarch64-none-elf-g++`
3. `make -j`，用 `/opt/devkitpro/libnx/switch_rules` 直接产出 NRO

两个必须记住的坑：

- **必须 `--platform linux/amd64`**。arm64 镜像里的 devkitA64 gcc 15.2.0 的 `cc1plus` 在这个
  aarch64 VM 里一编译就 ICE 段错误（连 3 行 hello.cpp 都挂）。
- 链接必须用 `aarch64-none-elf-g++`。`devkita64.sh` 不导出 `LD`，make 会退回系统
  `/usr/bin/ld`，报 `ld: unrecognised emulation mode: tp=soft`。

## 装到 Switch

```
sdmc:/switch/papa-walkman.nro
```

歌曲放 `sdmc:/music`（可递归 4 层）。hbmenu 里选「爬爬的随身听」即可；想要全内存/独占音频，
可以在主页**按住 R** 点任意游戏进 hbmenu（title takeover）。

## M1 能做什么

- 递归扫描 `sdmc:/music` 下的 MP3（最多 4 层）
- minimp3 解码，任意采样率/声道数 → 线性插值重采样成 48 kHz / 16 bit / 立体声
- libnx `audout` 输出，4 个缓冲轮转
- 屏幕显示当前曲名、进度、音量

| 按键 | 功能 |
|---|---|
| A | 暂停 / 继续 |
| ← / → | 上一首 / 下一首 |
| ↑ / ↓ | 音量 ±5 |
| + | 退出 |

## 已知限制

- M1 只支持 MP3；FLAC / WAV / OGG 在 M2 接入
- 没有 UI，只有 console 文字（磁带机界面在 M4）
- 没有索引缓存，每次启动全盘扫（M3 解决）
- applet 模式下如果别的程序占着音频，`audoutInitialize` 会失败并提示原因
