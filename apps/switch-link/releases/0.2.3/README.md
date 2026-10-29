# Switch Link 0.2.3

- `switch-link.nro`：已构建的主机程序，NACP 名称 `Switch Link 0.2.3`，4,035,785 字节。
- `SHA256.json`：上面这个 NRO 的校验值。
- 对应源码：`releases/0.2.1/switch-link-console-source.zip` 加上本文「与 0.2.1 的差异」里列出的版本号替换；第三方库与工具链版本没有变化，构建脚本仍是包内的 `switch-link/build-offline.sh`。

把 NRO 放到 SD 卡 `/switch/switch-link-023/switch-link-023.nro`，退出正在运行的旧程序后再从自制菜单打开（不要覆盖正在运行的那个 NRO）。电脑端用仓库 `apps/switch-link` 下的 `Start Switch Link.cmd` / `python3 server.py` 启动，两端在同一个局域网会自动发现。

## 0.2.3 改了什么

1. **版本号 0.2.1 → 0.2.3**，避免和卡上的 0.2.2 UTF8 诊断包混淆。
2. **电脑端不再把中文名直接写给主机**。实测这台机器的 fs 层建不出部分中文字符（`文`、`乐` 等返回 `0x202`），能建出来的名字又不出现在 `LIST`/`MLSD` 里。0.2.3 起 `switch_link/naming.py` 先把非 ASCII 名字按 UTF-8 字节做百分号编码再落盘（`周杰伦 - 七里香.mp3` → `%E5%91%A8%E6%9D%B0%E4%BC%A6 - %E4%B8%83%E9%87%8C%E9%A6%99.mp3`），网页列表显示时再解码回中文。纯 ASCII 名字完全不受影响，字面量 `%` 也不会被误伤。
3. 编码后超过 200 字符的文件名会在上传前报错，而不是写到一半失败。

副作用：主机端 App 自己的文件列表显示的是编码后的名字（还没做解码），电脑端网页、上传、下载、改名、音乐库都正常显示中文。

## 与 0.2.1 的差异（源码级）

对应源码包内的这些字符串从 `0.2.1` 改成 `0.2.3`：

- `sphaira/sphaira/CMakeLists.txt`：`set(sphaira_VERSION 0.2.1)`、`NAME "Switch Link 0.2.1"`
- `sphaira/sphaira/source/switch_link.cpp`：`"name"`、`"app_version"`
- `sphaira/sphaira/source/ui/menus/ftp_menu.cpp`：菜单标题
- `switch-link/overlay/` 下同名文件（与 `sphaira/` 里内容一致）

除此之外主机端代码没有改动；中文名修复全在电脑端 Python。
