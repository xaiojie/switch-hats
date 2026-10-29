# Switch Link 功能盘点与扩展路线

盘点对象：`apps/switch-link/console/`（NRO 主机端，Sphaira GPL-3.0 改版） + `apps/switch-link/switch_link/`（Mac 网页端）。
版本 0.2.1。NRO 产物 `apps/switch-link/releases/0.2.1/switch-link.nro`。

---

## 一、现在有什么

### NRO 主机端

| 模块 | 能力 | 实现位置 |
|---|---|---|
| 界面 | 两个标签：`01 / FILES`、`02 / INSTALL`；A 浏览 SD，B 返回 | `overlay/source/ui/menus/ftp_menu.cpp` |
| FTP | 端口 5000 匿名登录，根 `/sdmc:`，仅用于 SD 文件管理 | 上游 ftpsrv |
| HTTP 安装 | 自己从电脑拉 NSP/XCI 安装，**不走 FTP**，强制装 SD | `overlay/source/http_install.c` |
| 断点读取 | 按 HTTP Range 随机读取，限制在已验证大小内 | 同上 |
| 进度 | 用原 ProgressBox 回传真实步骤计数，百分比=当前步骤 | `overlay/source/ui/progress_box.cpp` |
| 结果回执 | 写 `/config/switch-link/last-install.json`，再 POST `{id,device_id,filename,success,result,registered_titles,message}`，最多 3 次 | `overlay/source/switch_link.cpp` |
| 发现 | UDP 28771 广播 `device_id / app_version / http_install_ready / command_port / http_formats` | `overlay/include/switch_link.hpp` |
| 命令 | UDP 28772 收 `switch-link-install`，每帧最多 8 包，非阻塞 | 同上 |
| 安全 | `device_id` 随机生成；能力 token 只在 URL；拒绝跨源/重定向/凭据/查询串；文件名 ≤200 字节、≤128 GiB、后缀白名单 | `switch_link.cpp` |
| 去重 | 任务号先落盘 `/config/switch-link/last-http-job`，重复包/重启不重放 | 同上 |

关闭的模块：NSZ/XCZ、USB HDD、MTP、光驱、音频。主题可换（`overlay/default_theme.ini`）。

### Mac 网页端（`127.0.0.1:8088`）

| 接口 | 功能 |
|---|---|
| `/api/discover` `/api/connect` `/api/devices` | UDP 自动发现、选中主机 |
| `/api/files` GET/DELETE、`/api/files/upload` PUT、`/api/files/download` GET、`/api/files/mkdir`、`/api/files/rename` | SD 卡列目录、上传、下载、新建目录、重命名、删除（经 FTP 通道） |
| `/api/library`、`/api/library/upload` | HTTP 游戏库：列出/加入本地 NSP/XCI |
| `/api/http-install` | 下发原生 HTTP 安装任务 |
| `/api/jobs`、`/api/jobs/cancel` | 任务查询与取消 |
| `/api/status` | 设备、库、任务、发现状态 |
| `/files/*` (`:8080`) | 静态游戏库，支持 Range（206） |

### 已知限制（今天实际撞到的）

- 安装进行中不能浏览 SD（互斥）；同一时刻只接一个安装任务。
- 页面里的设备令牌绑定服务进程，Mac 端一重启就报「主机已切换，请刷新页面」。
- 中断即从头开始，没有断点续传（`transfers` 里会堆一串 `interrupted`）。
- 局域网无配对认证，同网段任何人都能下发安装或删文件。
- 只保留最后一个任务号，防重放是「弱」的。
- 未真机验证：XCI 完整安装、错误/断网恢复、掉电行为。

---

## 二、能扩展什么

按「值得做 ÷ 代价」排序。P0 = 现在就疼，P1 = 明显提升，P2 = 锦上添花，P3 = 不建议。

### P0 · 立刻能感知的三件事

| 功能 | 为什么 | 怎么做 | 代价 |
|---|---|---|---|
| **断点续传 / 断线重连** | 现在一断从头来，8 GB 包一次中断就白等一小时 | 网页端记录已完成的 Range 区间（manifest 存 job 目录），重连后只补缺口；NRO 侧已有 Range 读取，只需接受「起始偏移」 | 中 |
| **相册 / 崩溃报告一键回收** | 今天看 DST 崩溃，全靠 FTP 手动一个个拉 | NRO 侧加一个「回传」动作：把 `/Nintendo/Album` 与 `/atmosphere/crash_reports` 增量同步到 Mac，按日期建目录；网页加一个「拉取」按钮 | 低 |
| **安装队列** | 现在一次只接一个，装完还要手动点下一个 | 网页端排队多个 job，NRO 侧装完一个自动接下一个；`last-http-job` 扩成小队列（带 32 位 id 列表） | 中 |

### P1 · 体验向

| 功能 | 说明 |
|---|---|
| 实时速度曲线 + 限速 | 已有 `speed/eta_seconds` 字段，网页画个曲线即可；限速能让安装时不占满 Wi-Fi（今天 4 MB/s 时看视频就卡） |
| 安装前识别包内容 | 解析 NACP/control，网页直接显示中文名、图标、版本号、最低系统版本、区服，避免装错区服 |
| 已安装清单导出 | 列 title_id / 版本 / 大小 / 位置（SD、emuMMC），导 CSV，Mac 侧比对「还差哪些」 |
| 安装时允许只读浏览 SD | 解开互斥：安装线程与浏览线程共享 FTP 会话池即可，风险可控 |
| 局域网测速页 | Switch↔Mac 的 TCP 吞吐一次性测出来，直接回答「为什么只有 1 MB/s」，今天靠 ping 和推断不够硬 |
| Wi-Fi 信息面板 | libnx 能拿到频段/信道/信号强度，提示「你在 2.4GHz，切 5GHz 能翻倍」 |
| 自动更新 | 启动时读 Mac 的 `releases/0.2.1/SHA256.json` 比对，提示新版并给下载地址 |
| 装完自动清理 Mac 副本 | 服务端已有回执（`success` + `registered_titles>0`），成功后可自动移入回收站 |

### P2 · 锦上添花

| 功能 | 说明 |
|---|---|
| SD 健康与测速 | 顺序读写测速、剩余空间、重复大文件扫描（NSP 残留最占地方，今天库就吃了 12 GB） |
| 存档备份/恢复 | 把某个 title 的 save 打包下载、可回滚；注意这是高风险区，必须二次确认 |
| 配对 PIN | 首次连接让用户在 Switch 屏上看到 4 位码，网页输入；补上「局域网无认证」这个洞 |
| 中文界面补全 | 现有串已中英混排（`S W I T C H   L I N K`、`01 / FILES`），统一到 i18n |
| 多主机 | 一个网页管两台 Switch（家里两台很常见） |

### P3 · 不建议

| 功能 | 原因 |
|---|---|
| 后台 sysmodule 常驻 | 崩了会影响整个系统，收益只是「不用打开 NRO」 |
| 网页重启到 payload / hekate | 一次误操作就变砖，风险远大于收益 |
| NSZ/XCZ 在线解压 | 主机 CPU 解码慢，还可能爆内存；在 Mac 上先解压更稳 |
| USB HDD 安装 | 本版已关，涉及驱动与权限，收益低 |

---

## 三、建议的下一步

1. **先把「回收」做掉**（P0 第 2 条）：代价最低，而且立刻解决「拉崩溃报告要手工一路点」的痛点。
2. **再做断点续传**（P0 第 1 条）：8 GB 级别的包，这个收益最大。
3. 队列和速度曲线可以一起做，都在网页端，不动 NRO 主流程。
4. 想解决「为什么慢」的争议，直接用局域网测速页给数字，不用再靠推断。
