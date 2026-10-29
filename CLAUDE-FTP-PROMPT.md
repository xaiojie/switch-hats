# 给 Claude Code 的提示词（直接整段复制粘贴）

> 背景：Claude 连 Switch FTP 报 `OSError: [Errno 65] No route to host`，并据此认定
> 「DBI FTP 端口没监听」，方向完全错了。下面是纠正版，核心原则是：**地址不写死，先发现再连接。**

---

```
停止猜测。先把下面几条读完后按顺序执行，不要自己编原因。

## 一、先查地址，不要写死 IP

Switch 的 IP 是路由器 DHCP 分配的，**每次可能不一样**，写死就是错的第一步。
仓库里已经有自动发现脚本，直接用：

    cd /Users/jay/开发/switch-hats/dst-mods
    python3 find_switch.py            # 输出 "192.168.1.75 5000"
    python3 find_switch.py --host     # 只要 IP，方便 $(...) 用

它的查找顺序：Switch Link 管理接口(127.0.0.1:8088) → 上次成功的缓存
→ 本机 ARP 邻居 → 扫本机网段找 5000 端口回 "220" 的主机。
连接参数固定为：端口 5000、anonymous、密码留空、被动模式（PASV）。

如果你看到的地址不是 find_switch.py 输出的那个，那就是错的。
你的会话历史里出现过 56 次 192.168.10.3，那是早已作废的旧网段，一律改掉。

## 二、服务器是好的，这一点已完整实测，不要再排查 Switch 端

同一台 Mac、同一时间窗口，实测结果：

  1. ping 通，3 packets, 0.0% packet loss
  2. TCP connect 5000 → OPEN
  3. 欢迎语 "220 Service ready for new user."
  4. 根目录 / → 只有 sdmc: 和 install:
  5. RETR /sdmc:/boot.ini → 成功，内容 "[payload]\r\nfile=payload.bin\r\n"
  6. STOR 15 字节 → 成功，SIZE 回读 = 15
  7. DELE 该文件 → 成功
  8. sdmc:/ 22 项、sdmc:/switch/ 27 项、sdmc:/atmosphere/contents/ 12 个 title
  9. 连续 RST 强断 3 次后立刻重连 → 每次都 OPEN，没有连接数限制或残留坏状态

结论：FTP 在线、可读可写可删、可反复重连。Switch 端不需要你动任何东西。

## 三、你上一条的结论是错的，不要沿用

你写的是：「connect() 本身失败，说明端口 5000 还没接受 TCP 连接，端口根本没有监听。」

两处事实错误：
  a) 端口没监听时内核回 RST，Python 报 ConnectionRefusedError（Errno 61），
     不是 EHOSTUNREACH（Errno 65）。你拿到的是 65。
  b) 你在 09:56:46 自己跑过 ping 192.168.1.75，三包全通、0% 丢包；
     两分钟后就断言「端口没监听」，前后矛盾。L3 通、L4 报 65 不是「服务没起」。

另外，这个 Switch 上跑的是 **Switch Link 自带的 FTP**，不是 DBI。
不要再提 DBI，不要建议「退出 DBI 重进 FTP 模式」，不要让用户去 Switch 上刷新地址。
仓库文档里残留的 DBI 字样和旧 IP 是历史遗留，以本节为准。

## 四、排查顺序（严格按序，做完一步再走下一步，重试不超过 2 次）

  第 1 步｜确认目标地址正确
    python3 find_switch.py --json
    把输出贴给我。确认 host 是当前实际地址、port 是 5000。
    不要假设、不要沿用脚本里或历史对话里的旧值。

  第 2 步｜分层定位，三条命令，结果原样贴出
    ipconfig getifaddr en0                  # Mac 自己的地址，必须和 Switch 同网段
    nc -vz <第1步得到的IP> 5000              # macOS 自带，最快
    python3 -c "import ftplib,sys;f=ftplib.FTP();f.connect(sys.argv[1],5000,timeout=10);print(f.getwelcome());f.quit()" <IP>

  第 3 步｜还失败就停下报告，不要改代码
    - nc 通但 ftplib 失败 → 贴完整 traceback，是 Python 侧问题。
    - nc 报 No route to host 而 ping 通 → 网络路径/拦截（代理、防火墙、
      macOS 本地网络权限）。这是环境问题，不是代码问题，你修不了，贴输出等我处理。
    - ping 也不通 → 先确认 Switch 没休眠、和 Mac 在同一个 Wi-Fi/网段。

## 五、禁止事项

  - 不要再说「端口没监听」「DBI FTP 没起来」「passive 模式问题」，这三个解释已被证伪。
  - 不要把 IP 写死在任何脚本、文档、命令里；一律走 find_switch.py。
  - 不要改端口、不要开主动模式、不要给 Switch 设静态 IP，除非我明确要求。
  - 不要在没验证连接的情况下编一套「原因」写给我。
  - 不要动 /Users/jay/开发/web游戏 或任何 web 服务器进程。
  - 不要重启、不要杀掉 8080/8088 上正在跑的 Switch Link 服务（自动发现要用它的接口）。

## 六、连上之后要做的事

    cd /Users/jay/开发/switch-hats/dst-mods
    python3 push_ftp.py            # 不给地址，脚本自己发现

  推 4 个文件到 sdmc:/atmosphere/contents/010090100E334000/romfs/：
    scripts/globalvariableoverrides.lua
    mods/modsettings.lua
    mods/dst-test-mod/modinfo.lua
    mods/dst-test-mod/modmain.lua

  推完脚本会自动逐文件回读 SHA256 与本地比对，看到「✓ 远端文件清单一致」才算完成。
  注意：Switch Link 的 HTTP 安装任务运行时不要同时传文件，会互相干扰。
```

---

## 补充：这次到底为什么失败

- 直接原因：Claude 把 `Errno 65 EHOSTUNREACH`（主机/路径不可达）当成
  `Errno 61 ECONNREFUSED`（端口没监听），于是编出「DBI 没进 FTP 模式」。
  而它在同一分钟内的 ping 是全通的，足以否定这个结论。
- 诱因：仓库文档里写着 `192.168.10.3` 和「DBI FTP」，它照抄了过期信息。
  这部分我已经清掉，并改成自动发现，IP 再变也不会带偏下一个 agent。
- 如果按上面第 1、2 步走完仍然报 65，那就不是文档问题，而是网络路径问题
  （代理拦截 / macOS 本地网络权限 / 不在同一网段），需要人工排查。
