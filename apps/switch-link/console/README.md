# Switch Link 0.2.1 主机端

基于 [Sphaira](https://github.com/NaGaa95/sphaira) 的 GPL-3.0 修改版。电脑网页发起任务，**Switch Link 自己通过 HTTP 读取并安装 NSP/XCI**。FTP 只用于 SD 文件管理，无需 DBI，也不通过 FTP 传输安装包。本构建关闭 NSZ/XCZ 支持。

## 使用与升级

将 `switch-link.nro` 放入独立目录 `/switch/switch-link-021/switch-link-021.nro`，关闭旧程序后，从 homebrew menu 打开 **Switch Link 0.2.1**。电脑和主机连接同一局域网即可自动发现。保持接收页面接收安装任务；按 A 浏览 SD，按 B 返回。新版不能在浏览 SD、弹窗或安装进度框打开时接收新的安装。

不要覆盖正在运行的旧版 NRO；旧版 RomFS 可能仍在读取它。源码构建产物在 `../releases/0.2.1/`；该发布目录同时提供对应源码与校验值。升级前可备份旧版，用户已确认使用新版后可清理旧目录。

安装位置强制为 SD。默认允许 emuMMC 安装、不允许 sysMMC 安装，继续遵循上游的安装检查。配置保存在 `/config/switch-link/`，不替换 hbmenu 或 Atmosphere 配置。真实安装仍需用户重新发起；启动不会自动重放上次任务。

## 通信与结果

- FTP 5000 匿名登录；SD 根目录 `/sdmc:`。0.2.1 关闭 FTP 虚拟安装入口。
- UDP 28771 广播原有设备标识、FTP 和真实任务进度，新增 `app_version:0.2.1`、`http_install_supported:true`、`http_install_ready`、`command_port:28772`、`http_formats:["nsp","xci"]`。
- UDP 28772 接收 `switch-link-install` 命令，同时保留原 `switch-link-task` 进度消息。非阻塞接收，每帧最多 8 包；安装命令最多 4096 字节，进度最多 2048 字节。
- 安装命令绑定本机 `device_id`、32 位十六进制任务号、UTF-8 至多 200 字节的文件名、完整 `wire_name` 和正整数大小（至多 128 GiB）。拒绝控制字符、路径、重复 JSON 字段、不支持的后缀和非法范围。
- `url` 必须为 UDP 发送方的 IPv4 HTTP 地址，路径 `/link-files/<id>/<64hex token>/<百分号编码的原文件名>`。`result_url` 必须同源，路径 `/link-result/<id>/<同一token>`。拒绝凭据、查询参数、片段和跨源地址；原生安装请求禁止重定向。文件按 HTTP Range 随机读取，限制在已验证大小内。
- 接收器在内存中立刻预留任务号，安装工作线程在任何 HTTP 读取前将任务号持久化至 `/config/switch-link/last-http-job`；重复包、忙碌时的新包及重启后最后一个任务号不会触发第二次安装。刷新后的临时任务号也用于中断恢复。只保留最后一个任务号，不是完整历史防重放数据库。
- 引擎通过原 ProgressBox 执行并回传真实步骤计数。安装百分比是当前步骤，应用记录注册阶段为不定进度；HTTP 流量完成不能证明安装成功。
- 安装线程在引擎返回后写入 `last-install.json`，并向能力 URL POST `{id,device_id,filename,success,result,registered_titles,message}`。只有 `result:0` 且 `registered_titles>0` 才成功；失败可能留下部分已注册内容，不能据此声称回滚。
- 结果 POST 最多 3 次，每次连接超时 2 秒、总超时 4 秒；不记录能力 URL。失败时仍保留本机回执和终态广播，电脑端不能仅凭广播/下载量宣称成功。

`device_id` 首次启动随机生成并保存，不含序列号或 MAC。能力 token 只出现在指定 HTTP URL，不广播、不放入普通状态。UDP 命令是受限的局域网协议，没有登录配对；请在可信局域网使用。

## 源码和构建

上游锁定到 `338348e74b6d278bc570a9be5373d25197bf6c8d`；libnx 锁定到 `b156b4774fb2df7b93402169509266da7547fc4c`。其余 FetchContent 依赖使用上游固定提交。`apply_customization.py <干净上游目录>` 可重建完整修改，`overlay/` 包含新增实现。`package_source.py` 打包实际参与构建的源码及依赖。

`build.ps1` 使用工作目录中的便携 devkitA64、libnx、deko3d、switch-cmake、switch-curl、switch-glm、switch-zlib、switch-mbedtls、CMake、Ninja 和 Git。关闭 NSZ、USB HDD、MTP、光驱、音频及网络挂载模块。对应源码 ZIP 提供离线构建入口 `build-offline.sh`、许可证、依赖源码和平台补丁。

运行 `tests/run-host-tests.ps1` 可用 Windows GCC 检查与 NRO 相同的 HTTP/进度解析器。`inspect_nro.py` 检查实际 NRO 头、资源边界、名称和版本，输出 SHA-256 与构建信息。主机启动、自动发现、FTP传输校验及NSP的原生HTTP安装已完成真机验证，详见 `hardware-validation.json`。XCI完整安装、错误/断网恢复和掉电行为尚未验证。
