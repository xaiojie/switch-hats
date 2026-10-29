# 爬爬的 Switch 游戏库

配合 **DBI Home server** 用的局域网安装面板。Mac 上当个网页服务器，Switch 上的 DBI 直接连过来装游戏，不用数据线。

```
~/SwitchGames/
├── server.py            # 服务器本体（Python 3，无第三方依赖）
├── 启动游戏库.command    # 双击启动
└── 游戏库/              # NSP / XCI 放这里，DBI 看到的就是这个目录
```

## 启动

双击 `启动游戏库.command`，或者在终端里：

```bash
cd ~/SwitchGames && python3 server.py
```

想换端口 / 换目录：

```bash
python3 server.py --port 8081 --dir "/Volumes/NAS/Switch"
```

启动后终端会打印面板地址，例如 `http://192.168.10.9:8080/`。浏览器打开就是管理面板。

## 在 DBI 里配置

1. 打开面板，复制「写进配置」那一行，例如：
   ```
   SwitchLibrary=ApacheHTTP|http://192.168.10.9:8080/files/
   ```
2. 打开 SD 卡上的 `switch/DBI/dbi.config`，找到 `[Network sources]`，把这一行粘到下面（去掉行首分号）。
3. Switch 打开 DBI 921，进入主菜单的「浏览 HTTP/FTP/Github 服务器…」（部分中文翻译显示为「正在浏览」），再选「SwitchLibrary」浏览、安装。旧版可能直接把网络源显示在主菜单。

> Switch 和 Mac 必须在同一个 Wi-Fi。优先连 5GHz，2.4GHz 装大游戏会很慢。

## 面板能干什么

- 拖拽上传 NSP / XCI / NSZ（支持大文件、断点显示进度）
- 查看库里文件、大小、修改时间、磁盘剩余
- 复制单个文件的下载链接、改名、删除
- 查看 DBI 最近的请求（确认 Switch 有没有连上来）
- **Switch 连接检测**：每 5 秒更新。网络安装只在收到 DBI 对 `/files/` 的实际访问后显示成功，提供来源 IP、最近时间和错误码；浏览器打开网页不算 Switch 已连接。较早的成功记录显示为历史访问，不能据此确认当前在线。FTP 则独立记录实际登录和读取目录的检测结果，也可以点「立即检测」刷新；已连接 FTP 时会重新读取当前目录。
- **「Switch 文件」标签页**：连上 Switch 的 DBI FTP（端口 5000），直接浏览、上传、下载、改名、删除 SD 卡里的文件（插件、自制程序、hekate 配置都走这里）

## 用「Switch 文件」管理 SD 卡

1. Switch 上打开 DBI → 主菜单 → **启动 FTP 服务器**（Run FTP server），记住屏幕上显示的 IP。
2. 网页切到「Switch 文件」标签页，填 Switch 的 IP，端口 `5000`，点「连接」。
3. 之后就能浏览 SD 卡：快捷按钮能直接跳到 `switch/`、`switch/.overlays`、`atmosphere/contents`、`bootloader`、`Nintendo`。
4. 拖文件进虚线框 = 上传到当前目录；每行可以下载 / 改名 / 删除；「新建文件夹」建目录。

插件放哪：

| 东西 | 放哪 |
|---|---|
| Ultrahand / Tesla 的 `.ovl` 插件 | `switch/.overlays/` |
| 常驻插件（sysmodule，如 sys-clk、sys-patch） | `atmosphere/contents/<TitleID>/exs/` 或按插件说明 |
| 自制程序 `.nro` | `switch/` |
| hekate payload | `bootloader/payloads/` |
| 开机图 / 图标 | `bootloader/bootlogo.bmp`、`bootloader/res/` |

> 分区说明：SD 卡可读写（走 DBI FTP）；机身内存 USER / SYSTEM 只能靠 USB + DBI 的 MTP 只读访问，网页做不了；游戏卡在 DBI 里直接安装。FTP 和网络安装不能同时跑，传完文件退出 FTP 再去装游戏。

## DBI 那边看到的是什么

面板把 `游戏库/` 目录映射成 `/files/`，生成的是 Apache 风格的 HTML 目录列表（`<a href>` 链接），DBI 的 `ApacheHTTP` 类型能解析。文件下载支持 `Range`，DBI 可以断点续传。

## 注意

- **只给局域网用**，别把端口映射到公网，这服务没有登录验证。
- 「Switch 文件」用的是 DBI 的匿名 FTP，同一局域网里谁都能连——不用的时候记得在 DBI 里退出 FTP 服务器。
- 首次启动 macOS 可能弹防火墙提示，允许 Python 接入局域网。
- Mac 的 IP 变了，DBI 里那行配置也要跟着改。建议在路由器里给 Mac 绑个固定 IP。
- 端口被占用时会自动往后找（8080→8081…），以终端打印的地址为准。
- 上传同名文件会直接覆盖。
- 停止：终端里 `Ctrl+C`。

## 常见问题

**DBI 里看不到条目**
确认 URL 结尾是 `/files/`，类型写的是 `ApacheHTTP`，并且 Mac 和 Switch 能互相 ping 通（Switch 上可以进「互联网设置」看 IP 段）。

**上传到一半断了**
重新拖一次同名文件即可，会覆盖重传。

**想换库目录**
把 `--dir` 指过去，或者直接把文件拖进 `游戏库/`。目录不存在会自动创建。

## 卸载

删掉 `~/SwitchGames` 整个文件夹就行，没有系统级安装。
