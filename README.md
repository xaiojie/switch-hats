# switch-hats

Switch 大气层（Atmosphere）整合包的自用配置：简体中文 Hekate、开机壁纸、启动项图标、大气层启动图，外加局域网游戏库服务器和 Switch Link 主机/网页工具。

不是从零的整合包，是在 **HATS 官方包** 基础上改出来的增量文件。原包去 [sthetix/HATS](https://github.com/sthetix/HATS) 下载，本仓库只放「改过的东西 + 还原脚本 + 素材」。

## 版本

| 组件 | 版本 |
|---|---|
| Atmosphere | 1.12.1（2026-10-16 官方正式版） |
| Hekatos / Hekate | 6.5.4，支持固件到 23.0.1 |
| Nyx 菜单 | 1.9.4 简体中文（easyworld/hekate 汉化版） |
| HATS 构建 | `HATS-2026-10-16-50f58f7` |
| Switch Link | 0.2.1，自制应用通过 HTTP 安装、FTP 文件管理 |
| Ultrahand Overlay | 2.5.3（含 sys-clk、Status Monitor、EdiZon、FPSLocker、emuiibo、sys-patch 等插件） |

HATS 原包 SHA256：`ad57d034cd7540f063b62d7db702f8a75aa691beb1f6e385014243005d4e04cf`

## 仓库结构

```
sd/                          # 直接覆盖到 SD 卡根目录（相对路径一致）
  bootloader/
    bootlogo.bmp             # Hekate 开机壁纸，720×1280 32bpp
    hekate_ipl.ini           # 四个启动项（已改成中文名）
    res/ofw|stock|sysmmc|emummc.bmp   # 启动项图标，192×192 32bpp
    sys/nyx.bin              # 简体中文 Nyx 菜单
  atmosphere/package3        # 已打入自定义启动图（8,388,608 B）
build/
  apply_assets.py            # 从 assets/ 生成上面这些文件
  insert_splash_screen.py    # Atmosphere 官方启动图注入脚本
assets/                      # 原始素材（AI 生成的鲸鱼娘图 + 绘图提示词）
game-library/                # 局域网游戏库服务器（DBI 边下边装 + FTP 管 SD）
switch-link/                  # 原生 HTTP 安装客户端、电脑网页、源码与发布文件

dbi/                          # DBI 921 + 简体中文补丁（见 dbi/README.md）
使用说明.txt                  # 给「拿到卡的人」看的简版说明
一键写卡.command              # 把整个整合包写进插入的 SD 卡
```

## 换到新卡

**方式一（推荐）**：原整合包解压好之后，用 `一键写卡.command` 写卡，再把本仓库 `sd/` 下的文件按相对路径覆盖上去。

**方式二（手动）**：

1. 原整合包内容整个复制到 SD 卡根目录
2. `sd/` 里的文件覆盖同名文件
3. 备份过 emuMMC 的，保留原来的 `emummc.ini`
4. 不要动 `Nintendo/` 和 emuMMC 分区

## Hekate 启动项

| 启动项 | 说明 |
|---|---|
| 爬爬 · 官方固件 | 原厂系统，不加载 CFW |
| 爬爬 · 半正版 SYSMMC | 真实系统 + Atmosphere |
| 爬爬 · 真实系统 SYSMMC | 真实系统（`emummc_force_disable=1`） |
| 爬爬 · 虚拟系统 EMUMMC | 虚拟系统（`emummcforce=1`），日常用这个 |

## 热键

- **特斯拉 / Ultrahand 菜单**：按住 `L` + `十字键↓`，再点一下 **右摇杆按下**
- 插件列表、超频、金手指、帧率锁定都在这里

## 个性化做了什么

| 位置 | 改了什么 | 怎么还原 |
|---|---|---|
| `bootloader/sys/nyx.bin` | 换成简体中文 Nyx（1,404,241 B） | 从 HATS 原包取回（474,549 B，sha `78e357e1…`） |
| `bootloader/bootlogo.bmp` | 鲸鱼娘开机壁纸 | 删掉即回默认图 |
| `bootloader/res/*.bmp` | 四个启动项图标 | 从 HATS 原包取回 |
| `bootloader/hekate_ipl.ini` | 启动项改成中文名 | 从 HATS 原包取回 |
| `atmosphere/package3` | 打入自定义启动图（PNG 经官方脚本写入） | 从 HATS 原包取回（未改动的 8,388,608 B） |

启动图重打一遍：

```bash
python3 build/insert_splash_screen.py assets/splash.png sd/atmosphere/package3
```

图标和壁纸重新生成：

```bash
python3 build/apply_assets.py assets/ <原整合包解压目录>
```

## 已知没做完的

- **Ultrahand 默认中文**：2.5.3 设置里没有 Language 项，需要补 `lang/zh-cn.json` 或改配置，尚未解决
- **Ultrahand 呼出键**：想从 `L+↓+RS` 改成 `L+↓`，未改
- **Nyx 菜单背景**：对应 `bootloader/res/background.bmp`，菜单内部主题改不了，待做
- **prod.keys**：按需从自己的主机转储到 `sd:/switch/prod.keys`；不入库、不随发布文件提供。
- **真机验证范围**：Switch Link 的 NSP HTTP 安装已验证；XCI完整安装、换网重连及掉电恢复仍待验证。

## Switch Link 0.2.1

这是当前使用的双功能工具：**网页发起任务，Switch Link 自己通过 HTTP 下载并安装到 SD 卡；FTP 用于 SD 文件管理**，无需 DBI 执行安装。

1. 从 [0.2.1发布目录](switch-link/releases/0.2.1/) 获取NRO，放入SD卡 `switch/switch-link-021/switch-link-021.nro`，从自制菜单打开。
2. Windows电脑安装Python3.11后，双击 `switch-link/Start Switch Link.cmd`，打开 `http://127.0.0.1:8088/#install`。
3. 选择自动发现的主机，在网页选择安装包或安装电脑游戏库中的现有文件；主机保持接收页面。当前构建支持NSP/XCI，NSP已完成实机安装验证。

首次选择后记住随机设备标识，无需反复输入IP。HTTP下载完成不等于安装完成，只有主机安装引擎与应用记录注册成功回执才显示成功。安装期间不同时操作SD文件或重启服务。

[详细使用说明](switch-link/README.md) · [对应主机源码与构建说明](switch-link/console/README.md) · [验证记录](switch-link/console/VALIDATION.md)。发布目录同时包含可用NRO、GPL对应源码归档和校验值；游戏库、密钥、日志、下载缓存及备份不会提交。

## 游戏库服务器（game-library/）

Mac 上跑一个网页服务：浏览器拖 NSP/XCI 进去 → Switch 上的 DBI 通过 Wi-Fi 边下边装；第二个标签页用 DBI 的 FTP（端口 5000）直接管理 SD 卡里的插件和文件。

```bash
cd game-library && ./启动游戏库.command
```

DBI 侧配置（`switch/DBI/dbi.config` 的 `[Network sources]`）：

```
SwitchLibrary=ApacheHTTP|http://<Mac 的局域网 IP>:8080/files/
```

细节看 [game-library/README.md](game-library/README.md)。

## 校验值

| 文件 | SHA256 |
|---|---|
| 中文 nyx.bin | `4bc820b382a648af2988cef4a27747c55b5f2705949d90e89229f4979d03e4b0` |
| 原英文 nyx.bin | `78e357e111b5d62158486d1c8683c40bbf5248996f6c929c1e63689cf22dac62` |
| HATS 原包 | `ad57d034cd7540f063b62d7db702f8a75aa691beb1f6e385014243005d4e04cf` |

## 来源与许可

本仓库只做配置和打包，所有组件的版权归原作者：

| 组件 | 上游 | 许可 |
|---|---|---|
| Atmosphere / package3 | [Atmosphere-NX/Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) | GPLv2 |
| Hekate / Nyx / nyx.bin | [CTCaer/hekate](https://github.com/CTCaer/hekate) | GPLv2 |
| 简体中文 Nyx | [easyworld/hekate](https://github.com/easyworld/hekate) | GPLv2（汉化） |
| HATS 整合包 | [sthetix/HATS](https://github.com/sthetix/HATS) | 见上游 |
| Ultrahand Overlay | [ppkantorski/Ultrahand-Overlay](https://github.com/ppkantorski/Ultrahand-Overlay) | GPLv2 |
| Switch Link / Sphaira | [NaGaa95/sphaira](https://github.com/NaGaa95/sphaira) | GPL-3.0，见 switch-link/LICENSE 与对应源码归档 |
| DBI / DBIPatcher | [rashevskyv/DBIPatcher](https://github.com/rashevskyv/DBIPatcher) | 见上游 |

`sd/atmosphere/package3` 与 `sd/bootloader/sys/nyx.bin` 是上游二进制，放这里只为方便自己换卡时直接覆盖；源码请到上表链接取。壁纸、图标、启动图由 `assets/` 里的图生成，图是自用素材。

本仓库不含任何任天堂游戏文件、密钥（`prod.keys`）或票据数据。

## 说明

自用配置，仓库公开。里面不含任天堂官方游戏数据；`package3` 是 Atmosphere 的二进制、`nyx.bin` 是 Hekate 的二进制，升级对应版本后需要重新替换或重打补丁。
