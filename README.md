# switch-hats

**一台 Switch OLED 的全套折腾记录**：大气层整合与美化、两个自制 Homebrew（HTTP 装包工具 + 音乐播放器）、以及一场还没打赢的《饥荒》mod 移植战。

不是整合包发行版，是**自用配置 + 自研代码 + 实验记录**的集合。每个子目录都能独立看懂。

![platform](https://img.shields.io/badge/platform-Nintendo%20Switch-E60012?logo=nintendo-switch&logoColor=white)
![cfw](https://img.shields.io/badge/CFW-Atmosphere%201.12.1-4B8BBE)
![firmware](https://img.shields.io/badge/firmware-22.5.0-555)
![toolchain](https://img.shields.io/badge/devkitA64-16.1.0-6E4C13)
![cpp](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![python](https://img.shields.io/badge/python-3.9%2B-3776AB?logo=python&logoColor=white)
![license](https://img.shields.io/badge/license-GPL--3.0%20%2B%20mixed-green)
![last commit](https://img.shields.io/github/last-commit/xaiojie/switch-hats?label=last%20commit)

---

## 一分钟上手

| 你想干什么 | 去哪里 |
|---|---|
| 换一张新 SD 卡、把系统装回去 | [cfw/README.md](cfw/README.md) → `cfw/一键写卡.command` |
| 在 Mac 上给 Switch 装 NSP/XCI（不用拔卡、不用 DBI） | [apps/switch-link/README.md](apps/switch-link/README.md) |
| 让 Switch 放歌 | 装 `apps/papa-walkman/` 的 NRO，歌放 `sdmc:/music/` |
| 编一个 Switch NRO | [docs/dev-environment.md](docs/dev-environment.md) |
| 查这台机器的固件/插件/IP/目录 | [docs/switch-hardware.md](docs/switch-hardware.md) |
| 连不上 Switch 的 FTP | [docs/device-access.md](docs/device-access.md) |
| 想知道《饥荒》mod 卡在哪 | [research/dst-mods/STATUS-2026-10-29.md](research/dst-mods/STATUS-2026-10-29.md) |

---

## 项目地图

| 目录 | 是什么 | 状态 |
|---|---|---|
| [`cfw/`](cfw/README.md) | HATS 整合包的中文化与美化：简体 Nyx、四启动项、开机壁纸、大气层启动图、写卡脚本 | 稳定使用中 |
| [`apps/switch-link/`](apps/switch-link/README.md) | **Switch Link**：主机端 NRO（Sphaira 改版）+ Mac 网页端。自动发现主机、HTTP 流式安装 NSP/XCI 到 SD、FTP 管文件、装完回执 | 0.2.1，NSP 实机验证通过；XCI 装到 69.6% 失败（疑似主机 SD 空间） |
| [`apps/papa-walkman/`](apps/papa-walkman/README.md) | **爬爬的随身听**：磁带机风格的音乐播放器，MP3/FLAC/WAV/OGG，歌词、库页、可取消的 HTTP 流播、断点恢复 | 0.3.0，源码与测试齐全 |
| [`research/dst-mods/`](research/dst-mods/STATUS-2026-10-29.md) | **饥荒 mod 移植研究**：LayeredFS 注入、十一轮实验、崩溃点反汇编、剩余假设 | **未解决**（`0x4A8` 原生崩溃） |
| [`tools/dbi/`](tools/dbi/README.md) | DBI 921/912 与简体中文补丁（备用的装包/文件管理手段） | 备用 |
| [`archive/game-library/`](archive/README.md) | 第一代局域网游戏库（DBI 边下边装），已被 Switch Link 取代 | 冻结 |
| [`docs/`](docs/README.md) | 机器参数、设备接入、开发环境、历史设计文档 | 持续更新 |

---

## 这台机器

| 项目 | 值 |
|---|---|
| 机型 / 固件 | Switch **OLED**（Mariko）/ **22.5.0** |
| 破解 | Atmosphere **1.12.1** + Hekate 6.5.4（HATS `2026-10-16-50f58f7`），RCM 注入 |
| 双系统 | **文件型 emuMMC** `sdmc:/emuMMC/SD00`，日常跑虚拟系统 |
| SD | 256 GB FAT32 |
| 常驻 | sys-clk、MissionControl、emuiibo、sys-patch、ldn_mitm、SaltyNX、nx-ovlloader |
| 特斯拉热键 | `L` + `十字↓`，再按右摇杆 |
| 局域网 | Switch `192.168.10.3:5000`（FTP，匿名）/ Mac `192.168.10.2:8080`（HTTP 库）、`:8088`（管理网页） |

完整参数（启动项全文、每个 sysmodule 的 title id 与配置路径、目录树、已知的坑）→ **[docs/switch-hardware.md](docs/switch-hardware.md)**。

---

## 三个子项目各解决了什么

### 1. Switch Link：不用拔卡、不用 DBI 装游戏

以前的做法是 Mac 上开个 HTTP 服务，Switch 上打开 DBI 手配源、边下边装，进度和结果全靠人盯。现在：

1. 网页端 UDP 广播发现主机（不用背 IP）
2. 选库里的 NSP/XCI → 网页把带一次性 token 的任务推给主机
3. 主机自己 `Range` 拉流、直接写 NAND，进度显示真实 NCA 步骤
4. 装完写回执 `/config/switch-link/last-install.json` 并回报网页

技术要点：Sphaira 改的 NRO、`ThreadingHTTPServer` 提供流、任务号去重防重放、主机端固定装 SD。
待做（P0）：**断点续传**（现在是中断即从头开始）、相册/崩溃报告一键回收、安装队列。完整路线见 [功能盘点](apps/switch-link/docs/FEATURES-AND-ROADMAP.md)。

### 2. 爬爬的随身听：Switch 上的磁带机

零外部依赖：音频走 libnx `audout`，解码 vendored（minimp3 / dr_flac / stb_vorbis），UI 自己画。0.3.0 把播放器拆成 `core/media/stream/decoder/engine/ui/...` 十来个模块，自带 C++ 单元测试（`tests/`，覆盖解码、重采样、暂停闸门、路径与模型）。

局限要写在前面：**不能后台播放**（退出即停）、**没有蓝牙耳机**（Switch 本体限制）、启动会接管 title（`title takeover`）。

### 3. 饥荒 mod：还没打赢

目标很简单——让 Switch 版《饥荒联机版》加载一个只加金块的测试 mod。结果是一路撞到同一个原生崩溃：

```
Result: 0x4A8 (2168-0002)  Type: Data Abort  Address: 0x0
Crashed thread: RenderThread
PC: dontstarve_nx.nss + 0x8ae4   →   ldr x8, [x0]   (x0 = *(x20+0x158)+0x90 = NULL)
```

十一轮实验把范围从「mod 一开就崩」缩到「modmain 确实执行、世界生成期渲染线程崩」，排除了 9 条假设，还剩 7 条（首选：把 `print` 探针换成 `io.open("/sdmc:/dst-probe.log","a")` 落盘）。全部证据、反汇编、复现命令在 [STATUS-2026-10-29.md](research/dst-mods/STATUS-2026-10-29.md)。

---

## 仓库结构

```
switch-hats/
├── cfw/                    # 大气层整合：sd/ 覆盖文件、assets/ 素材、tools/ 生成脚本
├── apps/
│   ├── switch-link/        # 主机端 console/（C++/NRO）+ Mac 端 switch_link/（Python/网页）
│   └── papa-walkman/       # C++17 + CMake + Ninja，source/ tests/ third_party/
├── research/dst-mods/      # build_overlay.py、push_ftp.py、find_switch.py、work/ 证据、STATUS
├── tools/dbi/              # DBI NRO + 中文补丁
├── archive/game-library/   # 第一代游戏库（冻结）
└── docs/                   # 机器参数、设备接入、开发环境、历史设计
```

## 开发与验证

```bash
# 编 NRO（本机 arm64，不用 Docker）
cd apps/papa-walkman && ./build.sh
cd apps/switch-link/console && ./build-macos.sh

# 跑测试
cd apps/switch-link && python3 -m unittest discover -s tests   # 89 用例，1 个 Windows 专属失败
```

环境怎么搭、踩过哪些坑（Cloudflare 按客户端指纹拦 `curl`、必须 `export LD`、镜像里 `cc1plus` 段错误…）→ [docs/dev-environment.md](docs/dev-environment.md)。

## 声明

- 本仓库**不含任何任天堂游戏文件、密钥（`prod.keys`）或票据数据**，也不提供下载渠道。
- `cfw/sd/` 里的 `package3`、`nyx.bin`、DBI 二进制均为上游发布物，仅为换卡方便而存放；版权与许可归原作者（见 [cfw/README.md](cfw/README.md) 与各子目录）。
- 自研代码（Switch Link 主机端与其改动的 Sphaira 部分、随身听、脚本与文档）按 **GPL-3.0** 发布，见 [apps/switch-link/LICENSE](apps/switch-link/LICENSE)。
- 折腾有风险，变砖/封号后果自负；先备份 `emuMMC` 和 `Nintendo/`。
