# Switch Link 0.2.3 主机端验证记录

日期：2026-10-28。此记录只涵盖本机编译、解析器和源码分发验证。

- CMake/Ninja 最终构建退出码 0，真实 AArch64 ELF 已链接并输出 NRO。使用便携 devkitA64 GCC 16.1.0、锁定 libnx 和上游依赖；系统安装目录未修改。
- 实际编译原生 UDP HTTP 命令接收、接收页消费任务、后台 HTTP Range 安装、SD 覆盖配置、原 ProgressBox、安装器真实计数、注册结果 POST、回执以及发现广播。
- `switch-link.nro`：4,035,785 字节。SHA-256：`50f8ddca571b2b8baffaa5a0bcd7c54ad1cf06981e3e23fef6266e1979916c70`。
- NRO0、ASET、JPEG、NACP 和 RomFS 边界检查通过；NACP 名称 Switch Link 0.2.3，版本 0.2.3。
- HTTP C 解析器先验证有效命令测试失败，再实现并验证通过；新增重复 JSON 字段测试先失败，再加入拒绝逻辑并通过。最终 `tests/run-host-tests.ps1` 编译运行两套实际生产 C 解析器，退出码 0。
- HTTP 测试覆盖设备/发送方 IPv4、HTTP 同源、任务号/token/文件名路径匹配、整数类型、3 GB 和 128 GiB 上限、越界、嵌入 NUL/控制字符、重复字段、200 字节文件名边界、中英文 UTF-8 字节长度、NSP/XCI 接受、NSZ/XCZ 拒绝、忙碌/接收页限制、最后任务号重放拒绝、Result 0 与注册条目数联合成功条件及 999 组畸形输入。
- 原进度测试覆盖设备身份、64 位计数、非法类型/范围、零速度、倒序计数及终态限制，另有 999 组畸形输入。
- 从干净上游 HEAD 重新应用完整 overlay，与增量构建树对比无内容差异；200 个文件仅有 Git Windows checkout 的 CRLF 差异。
- 初次构建发现便携 curl 头文件不支持 CURLOPT_PROTOCOLS_STR，改用其支持的 CURLOPT_PROTOCOLS/CURLPROTO_HTTP 后，最终构建通过。移除原进度框未使用局部变量，最终增量构建无编译警告。
- 对应源码包包含实际原生源码、libnx、FetchContent 依赖、portlib 源码、补丁、许可证、构建脚本、解析器测试和二进制构建信息；ZIP 完整性与内容一致性另外检查。

## 后续真机验证（2026-10-28）

- OLED/HOS22.5.0 emuMMC/Atmosphere1.12.1，已打开0.2.3接收页面并验证自动发现及HTTP安装能力报告。
- 用户从网页实际发起NSP安装，两次均取得匹配本次任务的成功回执：Result0，registered_titles=1。HTTP字节发送和终态广播未被用作成功证明。
- FTP文件上传及读回哈希一致，SD文件操作保留。
- 当前完整检查：后台61项、网页30项，加上述两套原生C解析器；结果均通过。
- `hardware-validation.json` 记录本次范围。构建信息和对应源码ZIP中的 `hardware_tested:false` 是构建时快照，未改动已发布源码归档的字节及哈希。

尚未验证：完整XCI安装、网络切换/断线恢复、掉电中断及所有取消路径。持久化只保留最后一个任务号，未提供完整历史反重放数据库。0.2.3关闭NSZ/XCZ和FTP安装入口，FTP文件管理保留。
