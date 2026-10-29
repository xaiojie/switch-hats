# 怎么连上这台 Switch（给人和 agent 的操作手册）

> 机器参数见 [switch-hardware.md](switch-hardware.md)。这里只讲「怎么连、连不上怎么判断」。

## 0. 前提

- Mac 和 Switch 在**同一个 Wi-Fi**（当前 Mac `192.168.10.2`、Switch `192.168.10.3`，都在 `192.168.10.0/23`）。
- Switch 上要有一个**正在前台跑的服务**：FTP 由 Switch Link 的 FTP 页面提供（DBI 也带 FTP），退出页面端口就关。
- 不要用旧 IP 硬编码。**IP 会变**，2026-10-29 就从 `192.168.10.3` 变过一次。

## 1. 先找地址（永远先做这一步）

```bash
python3 research/dst-mods/find_switch.py          # 打印发现的地址和来源
python3 research/dst-mods/find_switch.py --json   # 机器可读
```

查找顺序：显式参数/`DST_SWITCH_HOST` → Switch Link 接口 `http://127.0.0.1:8088/api/status` 的 `device.ip` → 缓存 `~/.cache/dst-switch-host` → `arp -an` 邻居 → 按 en0 掩码扫网段。命中判据是 **TCP 能连上且欢迎语是 `220`**，不是 ping 通。

## 2. FTP（文件读写）

```python
import ftplib
f = ftplib.FTP()
f.connect('192.168.10.3', 5000, timeout=10)   # 端口 5000，anonymous，密码空
f.login()
print(f.nlst('/'))          # ['sdmc:', 'install:']
print(f.nlst('/sdmc:/switch')[:5])
f.quit()
```

- 根目录只有两个虚拟节点：`sdmc:`（SD 卡）、`install:`（安装虚拟盘）。
- 路径写法带冒号：`/sdmc:/switch/xxx.nro`。
- 支持 `STOR` / `RETR` / `DELE` / `MKD` / `RMD` / `RNFR`+`RNTO`，中文文件名可用（UTF-8）。
- 卡是 FAT32：**不要用它传 >4 GB 的单文件**（安装包走 HTTP 流式安装，不走 FTP）。

现成脚本（推 `apps/../overlay` 到卡上）：

```bash
python3 research/dst-mods/push_ftp.py            # 自动发现地址
python3 research/dst-mods/push_ftp.py 192.168.10.3
```

## 3. HTTP

| 用途 | 地址 |
|---|---|
| 管理网页（只本机） | `http://127.0.0.1:8088/` |
| 游戏库静态文件 | `http://192.168.10.2:8080/files/<文件名>`，支持 `Range` |
| 状态接口 | `http://127.0.0.1:8088/api/status` |

装包流程：网页选主机 → 选库里的 NSP/XCI → 主机端从 `8080` 拉流 → 主机自己写 NAND。

## 4. UDP

- `28771`：主机广播发现（网页端无需知道 IP）。
- `28772`：网页端把安装任务（含一次性 token）推给主机。

## 5. 连不上怎么判断（别猜）

| 现象 | 含义 | 下一步 |
|---|---|---|
| `ConnectionRefusedError [Errno 61]` | 网通了，**端口没监听**（App 没停在 FTP 页 / 服务没起） | 让 Switch 回到 Switch Link 的 FTP 页面 |
| `OSError [Errno 65] No route to host` | **地址不对或不在同一网段**（旧 IP、别的 Wi-Fi） | 跑 `find_switch.py` 重新找，别照抄文档里的 IP |
| `timed out` | 主机休眠/断网/路由器隔离（AP isolation） | 唤醒主机、确认同一个 SSID |
| 能连上但列表为空 | 服务在但存储没挂好 | 重启主机端 App |

**先 `ping` 再 `ftplib` 是错的**：ICMP 通不代表端口开，端口开的判据是 TCP 握手 + `220` 欢迎语。

## 6. 安全

- FTP 是匿名明文，只在内网用；不要把 5000/8080/8088 映射到公网。
- Switch Link 的安装 URL 带一次性 token，且绑定目标主机与文件名，用完即失效。
- `/sdmc:/switch/prod.keys` 是本机密钥，**永不入库、不外传**。
