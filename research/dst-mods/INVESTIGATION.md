# Codex 接手后的实测记录（2026-10-28）

目标仍是：主菜单不崩，新建世界出生背包多一块金块。离线检查不等于 Switch 验收。

## 已核对的现场

- FTP 上原来还存在 `romfs/scripts/modindex.lua`（41,807 字节），与本地输出不一致。
  其内容是旧版完整脚本加目录枚举补丁。已备份并移除。
- 原 `push_ftp.py` 只上传，不发现残留文件。现在上传范围限定为本游戏的 `romfs`，
  上传后检查完整文件清单并回读 SHA256；残留文件或内容不一致会报错，不自动删除未知文件。
- `/tmp/dst_main_uncompressed` 的 NSO Build ID 与最新崩溃报告一致：
  `4D0E7A46016CAF6FA1A34144EF402E82AFE6E018000000000000000000000000`。
  这能支持使用该二进制解释崩溃地址，不能独自证明全部 Lua 脚本也与安装版一致。
- 解包脚本中 `main.lua` 先加载 `mods`、`modindex`，再加载 `globalvariableoverrides`。
  仅等待后续 `require` 的旧钩子不保证及时安装。
- mod 环境提供 `GLOBAL`，没有直接提供 `SpawnPrefab`。原测试 mod 在隔离环境下会报错；
  已改用 `GLOBAL.SpawnPrefab`，并限制在 `TheWorld.ismastersim` 上发放金块。

## 实验

| 轮次 | 覆盖层状态 | 用户实测 / 日志 |
|---|---|---|
| R1 | 移除远端旧 `modindex.lua`，其余 4 文件原样 | 仍崩；`01793200193_010090100e334000.log` |
| R2 | `--mode no-mod-load`：保留全局开关，但立即覆盖 `LoadMods` 为不执行 | 仍崩；`01793200480_010090100e334000.log` |
| R3 | `--mode sync-no-mod-load`：R2 加 mod 索引同步启动 | 仍崩；`01793200618_010090100e334000.log` |
| R4 | 整个 title 覆盖目录移出 `contents` | 用户确认原版可进主菜单 |
| R5 | `--mode vanilla`：原版 24 字节空 overrides 加三个 mod 文件 | 用户确认可进主菜单 |
| R6 | `--mode scoped-load`：全局保持关闭，只在原 `LoadMods` 执行期间开启；实际加载 mod | 无新增原生报告；截图为 `mods.lua:168 variable 'arg' is not declared`，已进入 prefab mod 回调 |
| R7 | R6 加共享 `runmodfn` 的显式参数捕获兼容补丁 | 已推送，等待菜单标记和金块验证 |

R1–R3 的新报告都保持相同签名：`0x4A8 / RenderThread / PC +0x8ae4 / X0=0`。
这些实验未证明 modmain 曾执行，不能沿用“已加载 mod 后才崩”的结论。
R4、R5 表明本次故障依赖注入脚本的行为，单纯增加 mod 目录未触发同样故障。
具体导致原生空指针的 Lua/引擎状态仍未定位。

## R6 的范围

R6 保留原版主机启动和菜单，不打开 PC 的 mod 页面。它立即修补已加载的
`ModManager.LoadMods` / `KnownModIndex.UpdateModInfo`，从 `mods/modsettings.lua`
读强制启用设置，在加载器返回或抛出 Lua 错误后恢复 `MODS_ENABLED=false`。

R6 截图已经证明测试 mod 执行及回调注册；`work/r6-result/2026102823294200-E760656D247D71BADAE2D642CAA986EF.jpg`
显示 `prefabpostinitany -> mainfunctions.lua:330 -> SpawnPrefab`，没有生成新的原生崩溃报告。
原 `runmodfn` 用 `unpack(arg)`，Switch 没有该隐含表。R7 通过 `debug.setupvalue` 替换
`GetPostInitFns` 引用的共享 `runmodfn`，保留安装版方法与原有错误处理，不替换整文件。

当前测试 mod 运行后，现有“创建游戏”文字后会出现 `[DST-MOD R7]`。新世界的金块是下一层证据。
这是零资产、纯 Lua 测试；尚未验证自定义资产、worldgen mod、联机或任意第三方 mod。

```sh
python3 build_overlay.py --romfs /tmp/dst_romfs --mode scoped-load
python3 push_ftp.py
uv run --with lupa python -m unittest discover -v
```

当前有 10 项离线检查，覆盖诊断模式、同步回调、局部开关恢复、隔离环境金块逻辑，
不提供隐含 arg 的 Lua 环境中的回调（含 nil 参数）、部署残留/损坏检测及裸文件名卸载。
它们不模拟原生渲染线程。

## 证据和备份

- 初始远端文件与前三份旧崩溃报告：`work/baseline-20261028/`
- R1、R2、R3 新报告：`work/clean-overlay-test/`、`work/r2-no-mod-load/`、`work/r3-sync-no-mod-load/`
- R3 移除前的完整本地备份：`work/r3-before-disable/`
- 卡上保留的 R3 备份：`/atmosphere/dst-mod-backups/010090100E334000-r3-20261028T151841Z`
- R6 已推送内容快照：`work/r6-scoped-load/`

不要把上述备份中的旧 `modindex.lua` 或整个 R3 目录当作修复版重新部署。
