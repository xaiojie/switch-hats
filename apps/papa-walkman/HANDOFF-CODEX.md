> **0.5.1 deployment verified 2026-10-30T12:39:02.271870+00:00**: unified Library, Settings and child pages with the Player design across all three themes; four Settings categories; 500 ms continuous lyric scroll, including duplicate-timestamp rows. Seek, track, document, theme and page changes reset the animation. NRO 3,937,573 bytes, full RETR SHA256 `fcf88126ec93a878240abc92efb4239ce48b1bcd9212a860976d34bc9d0a476b` matched; log.txt and trace.txt cleared and read back empty. ASan/UBSan 14/14; final affected tests 3/3; native build passed without warnings; independent review findings fixed and closed. Matching NRO/ELF/NACP in `build/diagnostics/release-0.5.1/`; Build ID `94dd951ec0c5e607fff965f0cc466e9b03edaa5c`. Details: `docs/validation-0.5.1.md`; deployment: `build/diagnostics/deployment-0.5.1.json`. **New-version hardware launch, playback and smoothness acceptance are still pending.** Original installation/title ID and the 0.4.0 full backup remain unchanged; 0.5.0 binaries plus the pre-change source snapshot are retained. Version-state entries below are historical.

> **0.5.0 deployment verified 2026-10-30T12:11:02.316332+00:00**: original NRO path overwritten via FTP 192.168.10.3:5000, 3,929,381 bytes; full RETR SHA256 matches `1fab607926b629468bcae83525d702ca16b0f77b1f421bb1c64b28359352cfdb`. Both log.txt and trace.txt cleared and read back empty. Deployment evidence: `build/diagnostics/deployment-0.5.0.json`; prior logs saved in `deploy-0.5.0.txt`. Awaiting first 0.5.0 hardware launch/playback check. Earlier NOT DEPLOYED records below describe the state before this upload.

> **2026-10-30 latest local build: 0.5.0, three native themes complete; NOT DEPLOYED.** Settings theme button / right-stick cycles AlbumLiner, NightRecord, LyricTheatre and persists the choice. ASan/UBSan CTest 13/13; final affected UI/interaction 2/2; 54 native previews. Independent review findings fixed and reread. NRO 3929381 bytes; SHA256 `1fab607926b629468bcae83525d702ca16b0f77b1f421bb1c64b28359352cfdb`. Matching binaries: `build/diagnostics/release-0.5.0/`. Details: `docs/validation-0.5.0.md`. Switch unavailable; keep original forwarder/title ID and overwrite the same NRO only when device returns, then read-back hash and clear logs. The verified 0.4.0 backup is unchanged. Earlier records below are historical.

> **2026-10-30 最新：0.4.0 界面／媒体／响应改进已完成本地代码检查和交叉编译，尚未部署。用户 Switch 不在身边，明确要求先完成代码与编译。** NRO 2,008,357 字节，SHA256 `5d6ebb9f06ed0f0f15fdf70239c6146e4cd02338f478e2736d36d33b50ad496e`。ASan／UBSan CTest 13/13；原生客户端实测 JSON/M3U 90 首歌曲身份一致，抽查封面和 24 行同步歌词成功。详细变更、证据与待真机验收项见 `docs/validation-0.4.0.md`。只允许原路径覆盖，未来推送仍需完整回读校验和清空日志。本版匹配 NRO/ELF 归档于 `build/diagnostics/release-0.4.0/`。
>
> **此前设备状态：0.3.12 已原位部署并完成真机启动/播放验证；初次弹窗来源尚未确认。** 以下为启动问题的历史记录。
> 最新根因已由 0.3.11 的 crash report 与匹配 Build ID 的 ELF 确认：HTTP 请求里的 `std::thread::detach()` 调用没有实现的 `pthread_detach`（返回 ENOSYS=88），抛出 `std::system_error` 后触发 terminate/abort。扫描线程进入 `__appExit` 关闭 SD/HID，主线程仍在初始化/读写，造成后续 devoptab 空指针和 HID `0xE401`。此前“其他越界写覆盖 newlib/HID 共享内存”的判断不成立。
> 修复覆盖 HTTP Open/Seek 两条路径：请求线程由管理器持有并 join，Switch 使用返回 Result 的 libnx threadCreate/threadStart；关闭应用时先取消并回收所有 HTTP 线程，再关闭 socket；日志看门狗也先停止并回收，再关日志文件。
> 0.3.12 NRO SHA256：`61fa380440fccce7d6637a078252fde52afd3dee4173bcec10b74bd631603679`（1,930,533 字节）。上传同一路径并 RETR 完整比对一致；两个日志文件均 STOR 空文件并 RETR 确认 0 字节。
> ASan/UBSan 下 CTest 12/12 通过。新增测试通过注入 ENOSYS 模拟 libnx 的不支持 detach 行为：旧实现可复现 terminate，新实现通过 HTTP 启动、跳转、Range、取消、截断、缓存恢复等测试。证据保存在 `build/diagnostics/` 与 `build-host/ctest-0.3.12.log`。
> 真机结果：已取回两次 0.3.12 的 log/trace，均正常进入主循环并 `RunApp 返回 0`；第二次运行 234.054 秒、6757 帧、取帧失败 0，云端 91 首加载与刷新成功。用户确认正常播放。无 PANIC/TERMINATE、无新增本应用 crash/erpt/fatal 报告。用户另报告首次有报错弹窗，但无文字/错误码，来源保留为未确认；不要据此盲改已通过的版本。
> `tools/inspect_nro.py` 的“16 个语言栏位均非空”断言仍不适用于本机 nacptool：0.3.11/0.3.12 均仅填 0–11 栏位。独立解析已确认 NRO 节区合法、版本正确、图标相同，且 NACP 除版本号外逐字节不变。

# Codex 接手说明：爬爬的随身听（Switch NRO）启动即失败

## 一句话问题

Switch 上启动「爬爬的随身听」会立刻回到桌面（有时黑屏、有时弹出系统报错框），无法进入界面。

## 工作目录

```
/Users/jay/开发/switch-hats/apps/papa-walkman
```

构建：

```bash
cd /Users/jay/开发/switch-hats/apps/papa-walkman
bash build.sh          # 产物 build/papa-walkman.nro
```

工具链已装在本机 `~/devkitpro`（`build.sh` 会自动 source `~/devkitpro/env.sh`）。

`build/papa-walkman.elf` 可直接反查符号，**必须先 source 环境，否则 addr2line 静默无输出**：

```bash
source ~/devkitpro/env.sh
aarch64-none-elf-addr2line -f -C -e build/papa-walkman.elf 0x<偏移>
```

libnx 源码在 `~/build/sl023/libnx`（含 `nx/source/...`），devkitPro 头文件在
`~/devkitpro/opt/devkitpro/devkitA64/aarch64-none-elf/include`。

## 传文件：Switch 上的 FTP

- 主机 `192.168.10.3`，端口 `5000`，anonymous，被动模式
- 每次开机需在 Switch 上手动打开 FTP 服务；关闭时 `Connection refused`
- NRO 位置：`/sdmc:/switch/papa-walkman/papa-walkman.nro`
- App 日志：`/sdmc:/config/papa-walkman/log.txt`、`/sdmc:/config/papa-walkman/trace.txt`
- 崩溃报告：`/sdmc:/atmosphere/crash_reports/`（按修改时间取最新；注意机器时钟不准）

仓库已带部署脚本（STOR 后回读校验 SHA256 / 拉日志 / 删日志）：

```bash
python3 tools/deploy.py --push              # 推送 + 回读校验
python3 tools/deploy.py --logs              # 取 log.txt 与 trace.txt
python3 tools/deploy.py --push --logs --clear   # 推送、取日志、清空
```

也可以直接用 `ftplib` / `curl -T`；curl 用法示例：

```bash
curl -T build/papa-walkman.nro ftp://192.168.10.3:5000/sdmc:/switch/papa-walkman/papa-walkman.nro --ftp-pasv -u anonymous:
```

## 当前状态

- 版本 **0.3.11** 已构建并部署到卡上
- `build/papa-walkman.nro` 1,926,437 字节，SHA256 `7d0888525b9e25b28a12176d2c5f2c1d4789403d9981ec67e50f8bf558d80bf1`
- 卡上 log.txt / trace.txt 已清空，等待一次干净启动

## 现象（实测记录）

启动后 1 秒内进程结束。不同版本表现：

| 版本 | 表现 |
|---|---|
| 0.2.3 | 19:07 崩溃报告：`Result 0x2B59 (2345-0021)`，`framebufferBegin` 内 `nwindowDequeueBuffer` 失败 → `diagAbortWithResult` → `svcBreak`；调用栈 `main.cpp:196` |
| 0.3.2–0.3.5 | 无崩溃报告，进程静默消失 |
| 0.3.3/0.3.4 | 崩溃报告 `Result 0x4A8 (2168-0002)` Data Abort，`Address 0x18/0x30`，PC 落在 `_close_r`、`_lseek_r`，栈为 `_fclose_r`/`__swrite`/`__sflush_r` ← `Logf` |
| 0.3.6–0.3.9 | 无崩溃报告，静默消失；日志停在启动早期 |
| 0.3.10 | 日志出现 `TERMINATE 未捕获异常` 后进程结束，无崩溃报告 |
| 0.3.11 | 尚未测试 |

0.3.10 的 trace.txt（已清空前）关键尾部：

```
[258] 看门狗 threadCreate rc=0x00000000
[322] framebufferCreate rc=0x00000000
[339] framebufferMakeLinear rc=0x00000000 stride=5120
[374] plInitialize 成功
[398] 字体加载 成功
[446] 配置读取完成：来源 2 个（启用 1）
[800] 扫描 threadCreate rc=0x00000000
[893] SD 扫描完成 0 首
[909] 拉取来源 ���%：��%
[924] 来源 ���% 返回 0 字节，错误=Expected HTTP/HTTPS URL
[941] TERMINATE 未捕获异常
```

## 需要知道的实现事实

- 工程用 **`-fno-exceptions`** 编译，代码里不能 try/catch；任何未捕获的 C++ 异常会 `terminate` → `abort` → `_exit`，**不留崩溃报告，进程静默消失**。
- 有两条日志通道：`source/log.cpp` 里 libnx fs 句柄直写 `trace.txt`（不走 newlib stdio），以及 newlib stdio 写 `log.txt`。
- 线程目前用 libnx `threadCreate`（看门狗线程、扫描线程）。
- `source/log.cpp` 的 `LogStartWatchdog()` 每 200 ms 对 newlib 全局 `devoptab_list` 附近作差异快照，变化写 trace.txt，可用来抓谁改写了 newlib 全局区。
- `svcGetInfo(InfoType_TotalMemorySize, CUR_PROCESS_HANDLE)` 打出来的 `3185/3189 MB` 不是本进程堆用量，忽略。
- 卡上音乐目录 `sdmc:/music`，配置 `sdmc:/config/papa-walkman/`，云端来源配置 `sources.json`。

## 硬性约束

1. **只原位替换 NRO**：不要改安装方式、不要改标题 ID、不要让用户重新安装。改完直接 `STOR` 覆盖同一个文件。
2. **必须改版本号**：`CMakeLists.txt` 里两处（`project(... VERSION ...)` 与 `nx_generate_nacp(... VERSION "...")`），`source/log.cpp` 里的 `BuildVersionMinor` 同步。
3. 推送后必须回读校验 SHA256，并清空 `log.txt`/`trace.txt` 再让用户启动。
4. 不要把「需要用户改卡上文件/重装」当作解决方案。

## 交付要求

- 自己构建、自己推送、自己取日志，不要让用户帮忙诊断
- 修到「启动能进界面并稳定运行」为止
- 最后只交付：根因一句话、改了哪些文件、新版本号、验证结果（日志/实测）
