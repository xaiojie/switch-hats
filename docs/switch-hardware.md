# 这台 Switch 的完整参数与配置

> 给接手的人 / 其他 AI agent 看的单一事实来源。所有值都在 2026-10-29 通过 FTP（`192.168.10.3:5000`）和 Switch Link 的 `/api/status` 实机读取，不是记忆里的默认值。
>
> 改了机器配置，请回来更新这份文件。

## 一分钟速查

| 项目 | 值 |
|---|---|
| 机型 | Nintendo Switch **OLED 版**（Mariko SoC，`/sdmc:/warmboot_mariko` 存在） |
| 系统固件 | **22.5.0**（Atmosphere 1.12.1 支持到 23.0.1，暂未升级） |
| 破解方式 | Atmosphere + Hekate（HATS 整合包），**RCM 注入**，`/sdmc:/payload.bin` + `boot.ini`=`file=payload.bin` |
| 日常启动项 | `爬爬 · 虚拟系统 EMUMMC`（`emummcforce=1`） |
| emuMMC | **文件型**，`/sdmc:/emuMMC/SD00`，`id=0x30304453`，非隐藏分区 |
| SD 卡 | **256 GB**，FAT32（单文件 ≤4 GB 限制 → XCI/NSP 都在 2–16 GB，靠 HTTP 安装流式读取，不落盘到 FAT32 大文件） |
| 序列号 | 已被 `blank_prodinfo_*` 抹除，读不到真实序列；不能登录任天堂在线服务 |
| 局域网 IP | `192.168.10.3`（DHCP，**会变**），Mac 侧是 `192.168.10.2` |
| 文件传输 | FTP `192.168.10.3:5000`，anonymous / 空密码，根 `/` 下只有 `sdmc:`、`install:` |
| 自制应用安装 | Switch Link NRO 自己 HTTP 下载并装 NSP/XCI（不经 FTP 传安装包） |

## 启动链与启动项

`/sdmc:/boot.ini`：

```ini
[payload]
file=payload.bin
```

`/sdmc:/bootloader/hekate_ipl.ini`（四个启动项，菜单里显示中文）：

| 启动项 | 关键参数 | 用途 |
|---|---|---|
| 爬爬 · 官方固件 | `ofw=1` | 原厂系统，不加载 CFW |
| 爬爬 · 半正版 SYSMMC | `stock=1`、`emummc_force_disable=1` | 真实系统 + Atmosphere，做系统级测试 |
| 爬爬 · 真实系统 SYSMMC | `emummc_force_disable=1` | 真实系统裸 Atmosphere |
| 爬爬 · 虚拟系统 EMUMMC | `emummcforce=1` | **日常用这个**，脏活都在虚拟系统里干 |

全局键：`autoboot=0`、`verification=1`、`backlight=100`、`autohosoff=2`、`autonogc=1`。

`/sdmc:/bootloader/nyx.ini`：`themebg=2d2d2d`、`themecolor=167`（中文 Nyx 的配色）、`timeoffset=1`、`timedst=1`、`bpmpclock=1`、`umsemmcrw=0`。

## 存储与虚拟系统

- `sysMMC` / `emuMMC` 双系统并存，emuMMC 是**文件型**：
  `/sdmc:/emuMMC/emummc.ini` → `enabled=1`、`sector=0x0`、`path=emuMMC/SD00`、`id=0x30304453`、`nintendo_path=emuMMC/SD00/Nintendo`。
- 虚拟系统的游戏和存档在 `/sdmc:/emuMMC/SD00/Nintendo/`；真实系统在 NAND 里。
- **SD 剩余空间很紧**（256 GB 卡装了 16 GB 的塞尔达 XCI、大量 NRO、主题和 emuMMC 备份），安装大包前先确认剩余空间 ≥ 包体大小 + 1 GB。装包是流式的（HTTP → 直接写 SD 的 NAND 区），但仍需要空间。
- `/sdmc:/` 顶层有：`atmosphere/`、`bootloader/`、`config/`、`emuMMC/`、`Nintendo/`、`switch/`、`music/`、`themes/`、`backup/`、`scripts/`、`SaltySD/`、`.mesa/`（Mesosphere shader cache）、`emuiibo/`、`payload.bin`、`boot.dat`、`boot.ini`、`hbmenu.nro`、`exosphere.ini`、`HATS_VERSION.txt`、`manifest.json`、`System Volume Information/`。

## CFW 组件版本（实机 `/sdmc:/HATS_VERSION.txt`）

| 组件 | 版本 |
|---|---|
| HATS 整合包 | `HATS-2026-10-16-50f58f7`（Builder 2.0.2-GUI），支持固件到 23.0.1 |
| Atmosphere | **1.12.1** |
| Hekate | 6.5.4 |
| Nyx 菜单 | 1.9.4 简体中文（easyworld 汉化版，`bootloader/sys/nyx.bin`） |
| DBI | 912（英文非官方版）+ 简体中文补丁 |
| KitNX | 1.0.2 |
| SaltyNX | 2.1.3 |
| Ultrahand Overlay | 2.5.3 |

## 常驻 sysmodule（`/sdmc:/atmosphere/contents/<title id>`）

| Title ID | 名字 | 作用 | 配置 |
|---|---|---|---|
| `0000000000534C56` | SaltyNX | 给 FPSLocker / 插件挂 hooks | `/sdmc:/SaltySD/` |
| `00FF0000636C6BFF` | sys-clk | CPU/GPU/内存超频 | `/sdmc:/config/sys-clk/config.ini` |
| `0100000000000352` | emuiibo | Amiibo 虚拟化 | `/sdmc:/emuiibo/`、`/sdmc:/config/emuiibo/` |
| `010000000000bd00` | MissionControl | 第三方手柄支持 | `/sdmc:/config/MissionControl/` |
| `420000000000000B` | sys-patch | 运行时打签名/版本校验补丁 | `/sdmc:/config/sys-patch/` |
| `4200000000000010` | ldn_mitm | 局域网联机中间层（替代 Nintendo 的 LDN） | `/sdmc:/config/ldn_mitm/` |
| `420000000007E51A` | nx-ovlloader | Tesla/Ultrahand 插件加载器 | 无 |
| `420000000007E51B` | ovlloader 配套模块 | 无 toolbox.json | 无 |
| `010090100E334000` | （残留）DST LayeredFS | 只有 `romfs/`，是饥荒 mod 实验的残留，**可以整目录删掉** | — |

空目录（无害，可删）：`0100E65002BB8000`、`05202F21AC184000`、`058CBC3C15300000`。

## Tesla / Ultrahand 插件（`/sdmc:/switch/.overlays/`）

`ovlmenu.ovl`（主菜单）、`Status-Monitor-Overlay.ovl`（温度/频率/分辨率）、`sys-clk-overlay.ovl`（调频）、`FPSLocker.ovl`（帧率锁定）、`sys-patch-overlay.ovl`、`ovlSysmodules.ovl`（看/管 sysmodule）、`ovlEdiZon.ovl`（金手指）、`emuiibo.ovl`、`ldnmitm_config.ovl`、`QuickNTP.ovl`（对时）。

呼出方式：按住 `L` + `十字键↓`，再按一下右摇杆（`RS`）。

## 自制应用（`/sdmc:/switch/`）

| 目录 / 文件 | 说明 |
|---|---|
| `switch-link-021/` | **Switch Link 0.2.1**（本项目，HTTP 原生装 NSP/XCI + FTP 管理） |
| `switch-link-022-utf8/` | 0.2.2 的 UTF-8 诊断版本，调试用 |
| `papa-walkman/` | **爬爬的随身听**（本项目，MP3/FLAC/WAV/OGG 播放器） |
| `DBI/` | DBI 912，用于 MTP/FTP/装包（备选手段） |
| `sphaira/` | Switch Link 的上游壳（文件管理） |
| `tinfoil/`、`Goldleaf/`、`CyberFoil/` | 其它装包/文件管理工具 |
| `JKSV/` | 存档管理 |
| `hats-tools/` | HATS 自带维护工具 |
| `linkalho/` | 账号假装已关联 |
| `daybreak.nro` | 系统更新（离线刷固件） |
| `haze.nro`、`reboot_to_payload.nro` | 大气层自带、重启回 payload |
| `prod.keys` | 本机密钥转储（**不要提交到任何仓库**） |

`/sdmc:/config/` 下对应目录：`switch-link/`、`papa-walkman/`、`sys-clk/`、`sys-patch/`、`sys-modules/`、`MissionControl/`、`tesla/`、`ultrahand/`、`edizon/`、`hats-tools/`、`kitnx/`、`wiliwili/`、`nx-hbmenu/`、`lockpick_rcm_pro/`、`status-monitor/`、`quickntp.ini` 等。

## 网络与对外服务

| 服务 | 地址 | 说明 |
|---|---|---|
| FTP | `192.168.10.3:5000` | anonymous / 空密码；根 `/` = `sdmc:` + `install:`；**只有 Switch Link 停在 FTP 页时才监听** |
| HTTP 安装 | 主机端从 Mac 拉流：`http://192.168.10.2:8080/files/...` | 由 Mac 上的 Switch Link 网页端提供，支持 Range |
| 设备发现 | UDP `28771` 广播 | 主机周期性广播，网页端靠它自动发现 IP |
| 命令通道 | UDP `28772` | 网页端把安装任务推给主机 |
| 管理网页 | Mac 上 `http://127.0.0.1:8088/` | 只监听本机 |
| 网页（局域网） | `http://192.168.10.2:8088/` | `--lan` 时开放，用来让手机/别的机器接管 |

**IP 会变**：Switch 走 DHCP，`192.168.10.3` 只是当前租约。写脚本不要硬编码，用 `python3 research/dst-mods/find_switch.py` 或 Switch Link 的 `/api/status`。想固定就进 Atmosphere 的网络设置配静态 IP（不建议改路由器 DHCP 绑定，因为换网络就失效）。

## 已知的坑

1. **FTP 只在主机端 App 停在 FTP 页面时可用**，退出页面端口就关，连接被拒（`Errno 61`）。看到 `Errno 65 No route to host` 才是网段/IP 不对。
2. **prodinfo 已抹除**（`blank_prodinfo_sysmmc=1`、`blank_prodinfo_emummc=1`）：不能登录 eShop/任天堂账号，联机只能走 ldn_mitm 局域网。
3. **`debugmode=1`**（`exosphere.ini`）：崩溃会写 `/sdmc:/atmosphere/crash_reports/`，这是排查崩溃的唯一现场，不要关。
4. **SD 是 FAT32**：单个文件写不进 4 GB 以上。XCI 安装是流式写 NAND 分区，不经过 FAT32 大文件，所以没问题；但**别用 FTP 传 >4 GB 的文件**。
5. **emuMMC 是文件型**：`emuMMC/SD00` 里的 `Nintendo/` 不要手动动，虚拟系统坏了大不了重做（`backup/` 有备份）。
6. **系统时间要准**：游戏和 TLS 都依赖它，偏差大了 Switch Link/随身听都可能失败；`QuickNTP.ovl` 可一键对时。
7. **固件 22.5.0 未升级**：升级要 `Daybreak`，且 HATS 包要支持新固件（当前支持到 23.0.1）。不要为了装游戏升系统。

## 健康检查（30 秒）

```bash
# 1. 主机在不在、服务在不在
curl -s http://127.0.0.1:8088/api/status | python3 -m json.tool | head -20

# 2. FTP 通不通、卡里有什么
python3 - <<'PY'
import ftplib
f = ftplib.FTP(); f.connect('192.168.10.3', 5000, timeout=10); f.login()
print(sorted(x.split('/')[-1] for x in f.nlst('/sdmc:/'))[:40]); f.quit()
PY

# 3. 最近一次崩溃（有的话）
python3 - <<'PY'
import ftplib, io
f = ftplib.FTP(); f.connect('192.168.10.3', 5000, timeout=10); f.login()
logs = [x for x in f.nlst('/sdmc:/atmosphere/crash_reports') if x.endswith('.log')]
if logs:
    newest = sorted(logs, key=lambda p: int(p.split('/')[-1].split('_')[0]))[-1]
    b = io.BytesIO(); f.retrbinary('RETR ' + newest, b.write)
    print(newest.split('/')[-1]); print('\n'.join(b.getvalue().decode('utf-8', 'replace').splitlines()[:8]))
else:
    print('没有崩溃报告')
f.quit()
PY
```
