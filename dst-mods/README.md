# Switch DST 最小 Lua mod 实验

目标：主菜单正常启动，新建测试世界后，出生背包多一块金块。

当前使用 **R7 / scoped-load**，实机验收状态见 `INVESTIGATION.md`。
该方案只验证零资产 Lua mod；不代表任意 Workshop mod、自定义资产或联机兼容。

## 当前方案

全局打开 `MODS_ENABLED` 的旧方案会在渲染线程发生 `0x4A8 / PC +0x8ae4` 崩溃。
移除覆盖层，以及只放原版空白 overrides 加 mod 文件，都已由用户实测可进菜单。
R6 改用局部开关后，故障变为可读的 Lua `arg` 错误，没有新增原生崩溃报告；R7 修补该错误。

R7 仅覆盖一个原版脚本 `scripts/globalvariableoverrides.lua`，在里面：

1. 保持 `MODS_ENABLED=false`，保留主机原有启动与菜单流程。
2. 对已加载的 `KnownModIndex.UpdateModInfo` 注入测试目录。
3. 只在原 `ModManager.LoadMods` 执行期间临时打开开关，正常返回或 Lua 报错后都恢复。
4. 对零资产测试 mod 移除其 assetpath 项。
5. 局部替换 `mods.lua` 的共享 `runmodfn` 闭包，显式捕获可变参数，修复 Switch 缺少旧式隐含 `arg` 表的问题。

不整文件覆盖 `mods.lua`、`modindex.lua` 或任何主菜单脚本。
测试 mod 通过标准 `AddPlayerPostInit` 注册回调，使用 `GLOBAL.SpawnPrefab`，只在主机模拟中发放金块。

## 构建与推送

Switch 端打开 Switch Link 的 FTP（端口 5000，匿名登录）。
Switch 的 IP 是 DHCP 分配的、会变，所以**不要在命令里写死地址**：`push_ftp.py` 会自动找。

```sh
cd /Users/jay/开发/switch-hats/dst-mods
python3 build_overlay.py --romfs /tmp/dst_romfs
python3 push_ftp.py            # 自动发现 Switch 的 IP
```

默认模式为 `scoped-load`。本轮请使用固定目录 `dst-test-mod`。
构建脚本会重建输出目录，并用模板覆盖测试 mod 内容，不要用它保管手工编辑的第三方 mod。

卡上的 `atmosphere/contents/010090100E334000/romfs` 应当恰有以下四个文件：

```text
scripts/globalvariableoverrides.lua
mods/modsettings.lua
mods/dst-test-mod/modinfo.lua
mods/dst-test-mod/modmain.lua
```

上传脚本会核对远端文件清单，并把文件下载回来校验 SHA256。
若卡上残留旧 `modindex.lua` 等文件，脚本会报错，必须先备份并清理该残留。
它不会自动删除清单外文件，也不会处理其他游戏的覆盖层。
title 目录外层由 Atmosphere 生成的 `romfs_metadata.bin` 不属于这四个文件。

## 实机验证

每次推送完成后退出 FTP 服务，完全关闭游戏再启动。

1. 主菜单能正常显示。
2. “创建游戏”文字后出现 `[DST-MOD R7]`，说明 `modmain.lua` 已执行。
3. 新建测试世界，出生后检查背包是否多一块金块。

出现错误时先保存截图，再重新打开 FTP 服务。
崩溃报告位于 `/atmosphere/crash_reports/`；截图位于 `/emuMMC/SD00/Nintendo/Album/`。
Lua 错误不一定产生新的原生崩溃报告，因此要同时检查截图和报告时间。

## 撤销

完全关闭游戏并打开 FTP 服务，然后运行：

```sh
python3 push_ftp.py --remove   # 同样自动发现
```

这会删除本游戏的覆盖目录，不删除游戏安装文件或存档。
若要保留覆盖层，可先把整个 title 目录移到 `/atmosphere/dst-mod-backups/`；
接手实验的 R3 已在该目录留下备份，具体路径见 `INVESTIGATION.md`。

## 离线检查与诊断模式

```sh
uv run --with lupa python -m unittest discover -v
```

| 模式 | 用途 |
|---|---|
| `scoped-load` | 当前 R7 方案，默认值 |
| `vanilla` | 原版空 overrides，保留测试 mod 文件；已实测可进菜单 |
| `normal` | 历史全局开启方案，已知原生崩溃 |
| `no-mod-load` | 历史 R2：全局开启但跳过加载，仍原生崩溃 |
| `sync-no-mod-load` | 历史 R3：R2 加同步索引启动，仍原生崩溃 |

离线检查覆盖 Lua 参数和回调行为、开关恢复、FTP 校验及卸载路径。
它们不模拟 Switch 渲染线程，不能代替实机验收。
