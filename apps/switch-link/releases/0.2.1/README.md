# Switch Link 0.2.1

- `switch-link.nro`：已构建的 Switch 主机程序，NACP名称 Switch Link 0.2.1。
- `switch-link-console-source.zip`：与此NRO对应的GPL源码、依赖源码、许可、补丁与离线构建入口；构建快照中的验证记录以构建时状态为准。
- `SHA256.json`：以上文件校验值。

将NRO复制为SD卡 `/switch/switch-link-021/switch-link-021.nro`，退出旧程序后从自制菜单打开；不要覆盖正在运行的NRO。电脑端用上两级目录的 `Start Switch Link.cmd` 启动。

当前源代码及后续真机验证记录见[主机端说明](../../console/README.md)和[验证记录](../../console/VALIDATION.md)。游戏安装由Switch Link自己通过HTTP执行，FTP管理SD文件。当前构建支持NSP/XCI，已验证NSP实际安装。
