修 Switch 自制 NRO「爬爬的随身听」启动即闪退。**要求一次性修好**：不要再交「诊断版本」让用户反复试，自己分析、自己改、自己构建部署验证。

工作目录：/Users/jay/开发/switch-hats/apps/papa-walkman
构建：cd /Users/jay/开发/switch-hats/apps/papa-walkman && bash build.sh（内部自动 source ~/devkitpro/env.sh）→ 产物 build/papa-walkman.nro
反查符号须先 source ~/devkitpro/env.sh，否则静默无输出：
  aarch64-none-elf-addr2line -f -C -e build/papa-walkman.elf <addr>

传文件：Switch 开着 FTP —— 192.168.10.3:5000，用户 anonymous，密码空，被动模式
  覆盖安装：STOR /sdmc:/switch/papa-walkman/papa-walkman.nro
  取日志：  RETR /sdmc:/config/papa-walkman/log.txt
            RETR /sdmc:/config/papa-walkman/trace.txt
  崩溃报告：LIST /sdmc:/atmosphere/crash_reports/
python3 一行：
  python3 -c "import ftplib,io;f=ftplib.FTP();f.connect('192.168.10.3',5000,timeout=30);f.login('anonymous','');f.set_pasv(True);f.storbinary('STOR /sdmc:/switch/papa-walkman/papa-walkman.nro',io.BytesIO(open('build/papa-walkman.nro','rb').read()));print('ok')"

现象：真机启动约 0.4 秒后闪退，最新版 0.3.7 为「直接闪退」；0.2.3 及更早是黑屏。

已定位的证据（可直接用，不用重复调查）：
- 崩溃固定发生在主线程写完「即将 hidInitializeNpad」之后的下一次日志写：
  _lseek_r → __swrite → __sflush_r → _fflush_r ← Logf ← source/main.cpp:178
- 崩因：devoptab_list[handle->device] == NULL，device=3（sdmc），_lseek_r 取 dev->seek_r 时解引用空指针（Address: 0x30）。
- 实测 314 ms 时该槽还是 sdmc，320 ms 已变 NULL，中间只有扫描线程开始 curl 拉取 m3u。
- 全程序能清空 devoptab 槽的只有 RemoveDevice（socketExit()、__appExit()），两者该时刻均未被调用 → 是别的写入把 devoptab_list[3] 覆盖成 0。
- devkitA64 newlib 布局：handles[] 1024 个指针 @0x1cdc80，__alloc_handle/__get_handle/__release_handle 有全局锁、fd 上界 1023；__stderr/stdout/stdin_handle 紧邻 devoptab_list @0x1cfcb0；devoptab_list[3] = 0x1cfcc8。
- 0.3.7 已有：写 stdio 前校验 devoptab（失效则跳过，不再崩）、fs 直写通道 trace.txt（libnx fs 句柄，不碰 newlib）、200 ms 看门狗（快照 devoptab_list-0x2430..+0x120）。已部署但用户仍报「直接闪退」。

要求：
1. 根因定位到具体那一笔越界/覆盖写入（快照偏移 + 反汇编 + 源码调用点），不要停在「devoptab 变 NULL」的现象层。
2. 修根因；同时做兜底：日志与所有文件 IO 不依赖可被破坏的 newlib state（fs 通道为准），任何 devoptab/handle 失效都不得导致崩溃。
3. 自己构建、FTP 原位覆盖部署、STOR 后 RETR 回读校验 SHA256，取回 log.txt/trace.txt/最新 crash_report 自行确认结果；不满意就继续修，不要回来问用户。
4. 交付时只说：根因是什么、改了哪几个文件、新版版本号、部署校验结果、需要用户做的唯一动作（启动一次）。

硬约束：
- 更新一律原位覆盖 /sdmc:/switch/papa-walkman/papa-walkman.nro（已挂到主页），不要改路径。
- 每次改版本号：CMakeLists.txt 的 project(papa_walkman VERSION x.y.z) 与 nx_generate_nacp(... VERSION "x.y.z") 两处。
- 不要问用户问题；不确定就在代码里选最稳妥的实现。
