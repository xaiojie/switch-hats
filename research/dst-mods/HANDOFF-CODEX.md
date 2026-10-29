# 交接给 Codex：Switch 版《饥荒联机版》加载 mod 原生崩溃

> **接手后更新（2026-10-28）：先看同目录 `INVESTIGATION.md`。**
> 已发现并移除远端残留的整文件 `modindex.lua`；R1–R3 仍然同签名崩溃，
> R4 无覆盖层、R5 原版空 overrides 加 mod 目录均可进主菜单。
> R6 `scoped-load` 已进入可读的 Lua 回调错误（缺少旧式 `arg`），无新增原生崩溃报告。
> R7 已局部修补该回调并推送，待金块验证。下文是原交接记录，部分结论已被新实验修正。
>
> **2026-10-29 更新：R8–R10 已由后续接手者完成，全部仍为同一 `0x4A8` 签名。
> 最新总账（时间线、已排除假设、下一步实验清单）见 `STATUS-2026-10-29.md`，先读那份。**

> 这份文档是给你的完整上下文，直接读就行，不需要再去翻聊天记录。
> 目标：让 Switch 版 DST 通过 Atmosphere LayeredFS 成功加载一个最小测试 mod。

---

## 0. 一句话问题

Switch 版 DST（`010090100E334000`）在打开 `MODS_ENABLED` 并让引擎真正加载 mod 之后，
**游戏在渲染线程（RenderThread）原生崩溃**：

```
Result: 0x4A8 (2168-0002)   Type: Data Abort
PC: dontstarve_nx.nss + 0x8ae4   LR: +0x8ac8   X[00] = 0
ReturnAddress: +0x10cac / +0x9008 / +0x2c35ec
```

崩溃时游戏其实已经"跑起来了"（有 Server×2、UpdateJobThread_0..4、AsynTextureLoadthread×2、
Ping、FMOD×3、NEX、NXInputManager、SystemService 都在运行），所以不是启动早期崩溃，
是**加载 mod 后、运行中渲染阶段**的空指针。

---

## 1. 工作目录

```
/Users/jay/开发/switch-hats/research/dst-mods/
├── build_overlay.py          # 生成 LayeredFS 覆盖层（核心脚本，改这里）
├── push_ftp.py               # 把 overlay 推到 Switch（FTP 5000）
├── overlay/                  # 生成的覆盖层（build_overlay.py 的输出）
│   └── atmosphere/contents/010090100E334000/romfs/...
├── README.md                 # 原理/装法/判断/撤销（中文，可直接读）
├── dst-mod-experiment.zip    # 早期打包版
└── HANDOFF-CODEX.md          # 本文件
```

环境：macOS，Python 3.9.6，`hactool` 编在 `/tmp/hactool/hactool`（需要 `-k /tmp/prod.keys`）。
解包出来的完整 romfs 在 `/tmp/dst_romfs`（2.7 GB，3240 个明文 Lua 脚本，是改脚本的"源码真相"）。
**注意 `/tmp` 可能被清空**——如果 `/tmp/dst_romfs` 或 `/tmp/hactool` 没了，需要重新解包（见第 7 节）。

---

## 2. 怎么跟 Switch 传文件（FTP）

Switch 上开着 **Switch Link 自带的 FTP**，端口 **5000**，匿名登录。
**Switch 的 IP 是 DHCP 分的，会变，不要写死。** `push_ftp.py` 已经内置自动发现
（`find_switch.py`：先问 Switch Link 的 127.0.0.1:8088 接口，再试缓存/ARP/扫网段），
直接不带地址运行即可；也可以 `python3 find_switch.py` 单独查当前地址。

```bash
# 安装/更新覆盖层（把 overlay/atmosphere/... 推到卡上同名路径）
cd /Users/jay/开发/switch-hats/research/dst-mods
python3 push_ftp.py

# 卸载（删 /atmosphere/contents/010090100E334000 整棵目录）
python3 push_ftp.py --remove
```

`push_ftp.py` 只做"上传"，**不提供下载**。如果你要从 Switch 拉崩溃报告/截图，有两个办法：

1. 用已有的 web 服务器 `/Users/jay/SwitchGames/server.py`（跑在 8080）的
   `/api/sw/download?path=...` 接口（GET，返回文件流），它封装了 Switch 的 FTP。
2. 或者自己写个十行的 `ftplib.FTP().retrbinary("RETR "+remote, ...)` 小脚本，
   先 `python3 find_switch.py --host` 拿到地址，再匿名登录即可。

**要用到的卡上路径：**

| 用途 | 路径 |
|---|---|
| 覆盖层根 | `/atmosphere/contents/010090100E334000/romfs/` |
| 改的脚本 | `.../romfs/scripts/globalvariableoverrides.lua` |
| 测试 mod | `.../romfs/mods/dst-test-mod/modinfo.lua`、`modmain.lua` |
| 强制启用 | `.../romfs/mods/modsettings.lua` |
| 崩溃报告 | `/atmosphere/crash_reports/<pid>_010090100e334000.log`（最新那个） |
| 截图 | `/emuMMC/SD00/Nintendo/Album/2026/10/28/*.jpg`（最新） |

---

## 3. 已经做了什么（全部只改 Lua，游戏本体字节没动）

所有覆盖都集中在**一个文件** `overlay/atmosphere/contents/010090100E334000/romfs/scripts/globalvariableoverrides.lua`
（由 `build_overlay.py` 生成）。做法是用 `require` 钩子注入 + 一个最小测试 mod。
**刻意避免整文件替换 `mods.lua` / `modindex.lua`**，因为已安装游戏版本比解包 NSP 新
（`multiplayermainscreen.lua` 报错行 908 vs 本地 988），整文件旧盖新会 `0x2F6202 User Break`。

`build_overlay.py` 当前做的事：

1. `MODS_ENABLED = true`、`DISABLE_MOD_WARNING = true`、`MODS_ROOT = "mods/"`。
2. `require("modindex")` 之后包 `KnownModIndex.UpdateModInfo`，调用前把
   `self.forceddirs["dst-test-mod"] = true`——因为主机上 `TheSim:GetModDirectoryNames()`
   返回空，`UpdateModInfo` 只认 `modnames`/`forceddirs`，不注入就根本读不到 modinfo。
3. `require("mods")` 之后包 `ModManager.LoadMods`，加载完调 `__strip_mod_assetpath()`：
   把 `package.assetpath` 里指向 `dst-test-mod` 的条目整个删掉。
4. `require("multiplayermainscreen")` 之后：补 `tooltipGetButtonStyle`（控制台版缺失）、
   吞掉 `OnModsButton`、包 `MakeMainMenu` 期间临时 `MODS_ENABLED=false`（不生成 Mods 按钮）。
5. 补 `TheSim.QueryTopMods` 空实现（PC 专有，主机没有，TopModsPanel 会崩）。
6. `mods/modsettings.lua` 里 `ForceEnableMod("dst-test-mod")` + `DisableModDisabling()`，
   绕开 Mods 界面强制加载。
7. 最小 mod：`modinfo.lua`（api_version=10）+ `modmain.lua`（`AddPlayerPostInit` +
   `DoTaskInTime(0)` → `GiveItem(SpawnPrefab("goldnugget"))`，出生给一块金块作为成功证据）。

---

## 4. 崩溃时间线（每次改完都推到机器上实测）

| 时间 | 改动 | 崩溃 |
|---|---|---|
| 22:02 | 首次开 MODS_ENABLED | Lua 报错 `multiplayermainscreen.lua:908: attempt to call field 'tooltipGetButtonStyle' (a nil value)` |
| — | 整文件替换 redux 文件 | 系统崩 `0x2F6202 User Break`（原生，栈 `open64→fopen→nss+0x43f80`），已回退 |
| 22:10 | require 钩子补 `tooltipGetButtonStyle` | 进菜单能看到 Mods 选项，点 Mods 崩 `topmodspanel.lua:71: attempt to call method 'QueryTopMods'` |
| 22:12~22:19 | 补 QueryTopMods + 吞按钮 + modsettings 强制加载 + 反斜杠 assetpath 修复 | **仍崩 `0x4A8 Data Abort 0x0 RenderThread`，签名完全一致** |
| 22:41 | 字节级 assetpath 反斜杠→斜杠 | 仍崩，签名不变 |
| 23:00 | 改为直接把 assetpath 里的 mod 条目删掉 | 仍崩，签名不变 |

**关键结论：** 按钮已不生成、点击已吞掉、assetpath 条目已删、反斜杠已修，
崩溃签名纹丝不动 → 崩溃点不在 Mods 按钮/页面/assetpath 反斜杠上，
而在**引擎真正加载 mod 之后的某条渲染路径**，该路径读取了某个持久化的、被 mod 影响的状态。

---

## 5. 逆向定位（已做，供你判断）

用 capstone 5.0.7 反汇编 `/tmp/dst_main_uncompressed`（main NSO，9,271,488 B；
**文件偏移 = 模块偏移 + 0x100**）。崩溃指令（模块 `+0x8ae4` = 文件 `0x8be4`）：

```asm
+0x8ad4: ldr x8, [x20, #0x158]   ; x20 是某渲染对象，+0x158 是子对象指针
+0x8ad8: cbz x8, #0x8a30         ; 非空才继续 → x8 非空
+0x8adc: ldr x0, [x8, #0x90]     ; x0 = *(x8+0x90) → 这里读到 0
+0x8ae0: mov v0.16b, v8.16b      ; SIMD 向量拷贝（纹理/颜色数据）
+0x8ae4: ldr x8, [x0]            ; ← CRASH：对 NULL 解引用
+0x8ae8: ldr x8, [x8, #0x10]
+0x8aec: blr x8                  ; 虚函数派发
```

即：**渲染对象 `x20` 的 `+0x158` 子对象存在，但该子对象的 `+0x90` 字段是 NULL**，
随后对 NULL 做虚函数派发。SIMD 指令说明这是渲染线程加载纹理/sprite/材质的图形路径。
崩溃函数起点约在模块 `+0x8934`，之前有一段遍历数组、校验 `[x20+0x70] == 4`、
`bl #0x51b24` 的逻辑。

调用链（三层虚函数派发）：
`+0x2c35ec`（外层，`blr x8`，x8=[x8,#0x10]）→ `+0x9008`（`blr x8`，x8=[x8,#0x30]）→
`+0x10cac`（`bl #0x8934`，进入含 0x8ae4 的函数）→ 崩溃。

---

## 6. 源码事实（`/tmp/dst_romfs/scripts/`，行号以解包版为准）

`mods.lua:442` 起 `ModWrangler:LoadMods(worldgen)`：

```lua
if not MODS_ENABLED then return end
KnownModIndex:UpdateModInfo()
-- ...
local moddirs = self:GetModsToLoad(self.worldgen)  -- :468
for _, modname in ipairs(moddirs) do
    local mod = KnownModIndex:GetModInfo(modname)
    local env = CreateEnvironment(modname, self.worldgen)   -- :270
    table.insert(self.mods, env)
end
-- 第二个循环（按序）：
package.path = MODS_ROOT..mod.modname.."/scripts/?.lua;"..package.path
table.insert(package.assetpath, {path = MODS_ROOT..mod.modname.."\\", manifest = manifest})  -- :537
self:InitializeModMain(modname, mod, "modworldgenmain.lua")  -- :542
self:InitializeModMain(modname, mod, "modmain.lua")          -- :548
```

`InitializeModMain`：`local fn = kleiloadlua(MODS_ROOT..modname.."/"..mainfile)`
→ 是 string 记 `failedmods`，nil 则跳过，否则 `RunInEnvironment(fn, env)`。

`manifest = mod.modname` 仅当 workshop 或 `forcemanifest` 才非 nil；本地 mod 时 `manifest = nil`。

依赖链：`main.lua:251 require("mods")`、`main.lua:306 require("modindex")`、
`modindex.lua:1 require("mods")`、`mods.lua:2 require "modutil"`、
`mods.lua:944 ModManager = ModWrangler()`（全局单例）。

其他已排除项：
- `GetActiveCharacterList` 是 C 函数（Lua 里无定义），不是崩溃源。
- mod 没图标（`modinfo` 无 `icon`/`icon_atlas`），`modindex.lua:544` 对缺失图标置 nil，
  `modstab.lua:1111` 只在有 icon 时 `SetTexture` → 排除图标相关崩溃。
- `CreateEnvironment` → `InsertPostInitFunctions`（`modutil.lua:178`）→
  `AddPlayerPostInit`（`modutil.lua:503`）只是 `table.insert`，加载期安全。

---

## 7. 如果 `/tmp` 被清空，怎么重建

```bash
# 1) 编译 hactool（需要 mbedtls 或自带 crypto；这台机器之前编过在 /tmp/hactool）
#    没有的话：git clone --recursive https://github.com/SciresM/hactool /tmp/hactool && make

# 2) 解包 NSP（拿到 prod.keys）
#    prod.keys 在 /tmp/prod.keys（如果没了，从 ~/.switch/prod.keys 或 Lockpick_RCM 输出复制）
/tmp/hactool/hactool -k /tmp/prod.keys -x --intype=pfs0 /tmp/dst_nsp/xxx.nsp ...

# 3) 从 NCA 里解出 romfs（需要 title key）
#    DST 的 Rights ID: 010090100E334000000000000000000D
#    title key: 51ea0132ef81d5803296bbd7d24de7c7
#    过程：hactool -k prod.keys --titlekey=... --romfsdir=/tmp/dst_romfs main.nca
```

更省事的备选：网上有现成的 `DST romfs` 解包分享，或直接从 SD 上用 DBI 的 MTP 拉游戏
（但 NAND 里的游戏是加密 NCA，还是得解）。
另外有一个可执行的思路：**直接从 SD 卡里已安装游戏的文件里 dump romfs**（NCA 加密，
需要 console key + title key，本机有）。

---

## 8. 交接时请注意 / 约束

- **不要整文件替换 `mods.lua` / `modindex.lua` / `multiplayermainscreen.lua`**——
  已安装版本比 NSP 新，旧盖新会 `0x2F6202 User Break` 原生崩。优先用
  `globalvariableoverrides.lua` 的 `require` 钩子做局部 patch。
- `TheSim` 在 Lua 里是个 table，可以直接 `TheSim.Xxx = function(self,...) end` 补方法。
- 主机版引擎很多 PC 专有方法缺失（已知：`tooltipGetButtonStyle`、`QueryTopMods`），
  遇到 `attempt to call ... (a nil value)` 就先补空实现。
- 测试 mod 刻意"零资产"（没有纹理/动画/声音），所有崩溃都不是资产缺失造成的——
  如果你要加资产，记得 `package.assetpath` 的路径在主机上可能是反斜杠，需要正斜杠化。
- 每次改完：`python3 build_overlay.py --romfs /tmp/dst_romfs && python3 push_ftp.py`，
  然后让用户在 Switch 上重启进游戏测；崩了就去 FTP 拉最新截图 + 崩溃报告。
- 撤销：`python3 push_ftp.py --remove`，重启即恢复。

---

## 9. 建议的下一个方向（我已排查但没做实验的假设）

1. **`manifest = nil` 的 assetpath 条目**：本地 mod 的 `manifest` 是 nil，主机版渲染/资产
   系统读取该条目时可能解引用空 manifest。我已尝试"删掉整个条目"仍崩 → 要么删得不彻底
   （`LoadMods` 之后又有人加回来/或 `package.assetpath` 不是唯一来源），要么根因不在这。
   可以验证：在 `LoadMods` 后打印/落盘 `#package.assetpath` 和每个 entry 的 path，确认删干净。
2. **`package.path` 里那句 `MODS_ROOT..modname.."/scripts/?.lua;"`**：只是 Lua require
   搜索路径，理论上不碰渲染线程，但如果主机版 `require` 缓存/加载器有别的问题，可试去掉。
3. **`CreateEnvironment` / `RunInEnvironment` 的环境隔离**：modmain 在独立 env 里跑，
   如果 env 的 `_G` 元表让某些原生对象跨环境传递时丢了 C 指针，可能在后续渲染时崩。
4. **mod 目录本身**：`mods/dst-test-mod/` 在 romfs 里是 LayeredFS 叠加的"新目录"，
   主机版 romfs 只读挂载可能对"运行期新增目录"支持不全。可试把 mod 目录预置到
   **游戏本体 romfs 里**（用 LayeredFS 的完整替换而非新增），或改成 SD 根下 `mods/` 用
   `MODS_ROOT` 指向绝对路径（需确认引擎是否接受非 romfs 路径）。
5. **版本对齐**：最稳的验证是找到与已安装版本**完全一致**的 NSP 重新解包，
   让 `globalvariableoverrides.lua` 的注入点行号/上下文与安装版一致。

---

## 10. 崩溃报告与截图（最新，供参考）

- 崩溃报告：`/atmosphere/crash_reports/01793198457_010090100e334000.log`（22:41，120,683 B）
- 截图：`/emuMMC/SD00/Nintendo/Album/2026/10/28/2026102822410700-57B4628D2267231D57E0FC1078C0596D.jpg`
- 截图内容（OCR 确认）：系统弹窗「由于发生错误，软件已关闭。/ OK / A 确定」= 原生崩溃，
  **没有 Lua 错误文本**（Lua 报错会被游戏自己捕获显示，这里是直接崩进程）。

---

## 11. 一句话给 Codex 的启动指令

> 读 `/Users/jay/开发/switch-hats/research/dst-mods/HANDOFF-CODEX.md` 和同目录 `README.md`，
> 目标是把 Switch 版 DST 的测试 mod 加载通。核心脚本是 `build_overlay.py`，
> 推送到 Switch 用 `python3 push_ftp.py`（FTP 5000 匿名，地址自动发现），
> 崩溃报告和截图也在同一 FTP 的 `/atmosphere/crash_reports/` 和
> `/emuMMC/SD00/Nintendo/Album/` 下。改完推上去，让用户重启进游戏验证。
