#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 Switch 版 DST 最小 Lua mod 的 LayeredFS 实验覆盖层。

输入：解包出来的 romfs（默认 /tmp/dst_romfs）
输出：overlay/atmosphere/contents/010090100E334000/romfs/...

默认 scoped-load（R7）：保留主机启动流程，仅在原 LoadMods 内开启 MODS_ENABLED；
局部修补 mod 目录枚举及旧式 arg 回调，不整文件替换 modindex.lua / mods.lua。
normal / no-mod-load / sync-no-mod-load 是历史失败对照，vanilla 是原版空脚本对照。

用法：
  python3 build_overlay.py --romfs /tmp/dst_romfs
  python3 build_overlay.py --romfs /tmp/dst_romfs --mods "dst-test-mod"
"""

import argparse
import shutil
import sys
from pathlib import Path

TITLE_ID = "010090100E334000"
OVERRIDES = """-- 爬爬的 DST / Switch mod 实验：打开 mod 系统
-- 由 build_overlay.py 生成，覆盖 romfs/scripts/globalvariableoverrides.lua
MODS_ENABLED = true
DISABLE_MOD_WARNING = true

-- 引擎在 PC 上给的是 "../mods/"（见二进制里的 "[string \\"../mods/" 字面量），
-- 主机上那个相对路径解析不到 romfs 内的目录，所以直接指到 romfs 根下的 mods/。
MODS_ROOT = "mods/"

-- 注入的测试 mod 目录名（要和 mods/ 下实际目录一致）
local __DST_MOD_NAMES = { "dst-test-mod" }

if MODS_ENABLED then
	local __real_require = require

	-- 引擎在主机上枚举不到 romfs 内的 mod 目录（TheSim:GetModDirectoryNames 返回空），
	-- 而 UpdateModInfo 只认 modnames/forceddirs。这里在它运行前把 forceddirs 填上，
	-- 让它去 LoadModInfo 把 known_mods 里的 modinfo 读出来，GetModsToLoad 才能往下走。
	local function __inject_mod_dirs()
		if KnownModIndex ~= nil and KnownModIndex.__dst_injected == nil then
			KnownModIndex.__dst_injected = true
			local orig_umi = KnownModIndex.UpdateModInfo
			KnownModIndex.UpdateModInfo = function(self, ...)
				for _, n in ipairs(__DST_MOD_NAMES) do
					self.forceddirs[n] = true
				end
				return orig_umi(self, ...)
			end
		end
	end

	-- mods.lua 的 LoadMods 会 table.insert(package.assetpath, {path = MODS_ROOT..modname.."\\",
	-- manifest = manifest})。我们的测试 mod 没有任何资产（纹理/动画/声音），根本不需要进
	-- assetpath；主机版引擎扫描这个空 mod 目录时会在渲染线程空指针崩溃
	-- （0x4A8 Data Abort @ 0x0, RenderThread，x20+0x158 对象 +0x90 字段为 NULL）。
	-- 所以在 LoadMods 跑完后，把指向测试 mod 的 assetpath 条目整个删掉。
	-- modmain.lua 走的是 kleiloadlua(MODS_ROOT..modname.."/modmain.lua")，不依赖 assetpath，
	-- 删掉不影响 mod 的 Lua 加载。
	local function __strip_mod_assetpath()
		if package ~= nil and package.assetpath ~= nil then
			for i = #package.assetpath, 1, -1 do
				local entry = package.assetpath[i]
				if type(entry) == "table" and type(entry.path) == "string" then
					for _, n in ipairs(__DST_MOD_NAMES) do
						if entry.path:find(n, 1, true) then
							table.remove(package.assetpath, i)
							break
						end
					end
				end
			end
		end
	end

	require = function(name)
		local mod = __real_require(name)
		if type(name) == "string" then
			if name == "modindex" then
				__inject_mod_dirs()
			elseif name == "mods" then
				if ModManager ~= nil and ModManager.LoadMods ~= nil and ModManager.__dst_wrapped == nil then
					ModManager.__dst_wrapped = true
					local orig_lm = ModManager.LoadMods
					ModManager.LoadMods = function(self, ...)
						local ret = orig_lm(self, ...)
						__strip_mod_assetpath()
						return ret
					end
				end
			elseif name:find("multiplayermainscreen", 1, true) then
				if type(mod) == "table" then
					-- 控制台版 redux 主菜单调用了 self.tooltipGetButtonStyle()，只在 PC 版存在
					if mod.tooltipGetButtonStyle == nil then
						mod.tooltipGetButtonStyle = function(self, text) return nil end
					end
					-- 主机版 Mods 界面会原生崩溃，点了就免战：吞掉点击
					if mod.OnModsButton ~= nil then
						mod.OnModsButton = function(self) end
					end
					-- 更彻底：建主菜单时临时把 MODS_ENABLED 关掉，Mods 按钮根本不生成
					if mod.MakeMainMenu ~= nil and mod.__dst_menu_wrapped == nil then
						mod.__dst_menu_wrapped = true
						local __orig_make = mod.MakeMainMenu
						mod.MakeMainMenu = function(self, ...)
							local prev = MODS_ENABLED
							MODS_ENABLED = false
							local ok, err = pcall(__orig_make, self, ...)
							MODS_ENABLED = prev
							if not ok then
								error(err, 0)
							end
						end
					end
				end
			end
		end
		return mod
	end
end

-- 控制台版没有 Workshop 排行榜接口：TopModsPanel 会调 TheSim:QueryTopMods，
-- 主机上这个方法不存在（PC 才有）。补个空实现，面板留空即可。
if TheSim ~= nil and TheSim.QueryTopMods == nil then
	TheSim.QueryTopMods = function(self, callback)
		-- 故意不回调：没有排行榜数据，面板保持空列表
	end
end
"""

MODINFO = """name = "爬爬测试 MOD"
description = "验证 Switch 版能否加载 mod：进入游戏时会拿到一块金块。"
author = "爬爬"
version = "1.0.0"

api_version = 10
dst_compatible = true
client_only_mod = false
all_clients_require_mod = false

priority = 0

server_filter_tags = {}
"""

MODMAIN = """-- 最小验证 mod：主机模拟中的玩家出生时给一块金块
local G = GLOBAL
local label = G.STRINGS.UI.MAINSCREEN.CREATE
if not string.find(label, "[DST-MOD R9]", 1, true) then
    G.STRINGS.UI.MAINSCREEN.CREATE = label .. " [DST-MOD R9]"
end
AddPlayerPostInit(function(inst)
    if G.TheWorld == nil or not G.TheWorld.ismastersim then
        return
    end
    inst:DoTaskInTime(1, function()
        if inst:IsValid() and inst.components.inventory ~= nil then
            local gold = G.SpawnPrefab("goldnugget")
            if gold ~= nil then
                inst.components.inventory:GiveItem(gold)
            end
        end
    end)
end)
"""


MODSETTINGS = """-- 不走 Mods 界面，直接在启动时强制启用测试 mod
-- （Mods 页在主机版会崩：TheSim:QueryTopMods 不存在、还有别的 PC 专有调用）
ForceEnableMod("%s")
DisableModDisabling()
DisableLocalModWarning()
"""

NO_MOD_LOAD = """
-- Diagnostic R2: retain MODS_ENABLED/startup callbacks, skip all mod execution.
-- main.lua loads mods before globalvariableoverrides, so patch the existing
-- singleton now instead of waiting for a later require("mods").
require("mods")
ModManager.LoadMods = function(self, ...)
    print("[DST probe R2 no-mod-load] skipped LoadMods")
end
"""

SYNC_STARTUP = """
-- Diagnostic R3: complete ModSafeStartup before main.lua returns to native code.
-- Only the mod index uses an in-memory startup; world/profile saves are untouched.
require("modindex")
KnownModIndex.Load = function(self, callback)
    self:UpdateModSettings()
    print("[DST probe R3 sync-startup] mod settings ready")
    callback()
end
KnownModIndex.BeginStartupSequence = function(self, callback)
    self.startingup = true
    print("[DST probe R3 sync-startup] initialize now")
    callback()
end
"""

SCOPED_OVERRIDES = """-- DST Switch R9: keep the console startup/menu path.
-- Experimental pure-Lua mod only; no custom assets or worldgen hooks.
MODS_ENABLED = false
MODS_ROOT = "mods/"
DISABLE_MOD_WARNING = true

-- The dedicated server instance manages mods through modoverrides.lua, not
-- through this file. Patching ModManager there is both unnecessary and may
-- stall the server startup (kleiloadlua path resolution differs per instance).
-- Only the frontend (player) instance needs the patches below.
if TheNet ~= nil and TheNet.IsDedicated ~= nil and TheNet:IsDedicated() then
    return
end

local names = { "dst-test-mod" }
require("mods")
require("modindex")

-- On the dedicated server instance, LoadMods calls LoadServerModsFile which
-- issues BeginServerModSetup + DownloadServerMods (Workshop/Steam download).
-- This test mod is local-only; that native network step has no work to do and
-- may block indefinitely on Switch. Replace it with a no-op before LoadMods runs.
if ModManager.LoadServerModsFile ~= nil then
    ModManager.LoadServerModsFile = function(self) end
end

-- mods.lua's shared runmodfn uses Lua 5.0's implicit `arg` table.
-- Switch has no such table. Replace only that shared closure upvalue;
-- keep the installed methods and their existing error-reporting behavior.
local function runmodfn_compatible(fn, mod, modtype)
    return function(...)
        if fn then
            local args = {n = select("#", ...), ...}
            local status, result = xpcall(function()
                return fn(unpack(args, 1, args.n))
            end, debug.traceback)
            if not status then
                print("error calling "..modtype.." in mod "..mod.modname..": "..tostring(result))
                ModManager:RemoveBadMod(mod.modname, result)
                ModManager:DisplayBadMods()
            else
                return result
            end
        end
    end
end
local patched = false
local upvalue_index = 1
while true do
    local name = debug.getupvalue(ModManager.GetPostInitFns, upvalue_index)
    if name == nil then break end
    if name == "runmodfn" then
        debug.setupvalue(ModManager.GetPostInitFns, upvalue_index, runmodfn_compatible)
        patched = true
        break
    end
    upvalue_index = upvalue_index + 1
end
assert(patched, "DST R9: cannot locate mods.lua runmodfn upvalue")

local original_update = KnownModIndex.UpdateModInfo
KnownModIndex.UpdateModInfo = function(self, ...)
    for _, name in ipairs(names) do
        self.forceddirs[name] = true
    end
    return original_update(self, ...)
end

local function pack(...)
    return {n = select("#", ...), ...}
end

local original_load = ModManager.LoadMods
ModManager.LoadMods = function(self, ...)
    -- Only inject mods in the frontend instance. The game instance (after
    -- LOAD_SLOT reset) has a different working directory; kleiloadlua("mods/...")
    -- cannot resolve paths there and will stall. The frontend instance is the
    -- only one that needs to run modmain for the menu marker and postinit hooks.
    if not IsInFrontEnd() then
        return original_load(self, ...)
    end
    KnownModIndex:UpdateModSettings()
    local previous = MODS_ENABLED
    MODS_ENABLED = true
    local result = pack(pcall(original_load, self, ...))
    MODS_ENABLED = previous

    -- This test mod has no assets. Keep its search entry out of native rendering.
    for i = #package.assetpath, 1, -1 do
        local entry = package.assetpath[i]
        if type(entry) == "table" and type(entry.path) == "string" then
            for _, name in ipairs(names) do
                if entry.path:find(name, 1, true) then
                    table.remove(package.assetpath, i)
                    break
                end
            end
        end
    end
    if not result[1] then
        error(result[2], 0)
    end
    return unpack(result, 2, result.n)
end
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--romfs", default="/tmp/dst_romfs")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent / "overlay"))
    ap.add_argument("--mods", default="dst-test-mod")
    ap.add_argument("--mode", choices=("normal", "no-mod-load", "sync-no-mod-load", "vanilla", "scoped-load", "trace-load"), default="scoped-load",
                    help="默认 scoped-load；其他模式仅作诊断，normal 是历史全局开启方案")
    args = ap.parse_args()

    romfs = Path(args.romfs)
    if not (romfs / "scripts" / "modindex.lua").is_file():
        sys.exit("找不到 %s/scripts/modindex.lua，先用 hactool 解包" % romfs)

    out = Path(args.out)
    root = out / "atmosphere" / "contents" / TITLE_ID / "romfs"
    scripts = root / "scripts"
    mods_dir = root / "mods"

    if out.exists():
        shutil.rmtree(out)
    scripts.mkdir(parents=True)

    overrides = OVERRIDES
    if args.mode in ("no-mod-load", "sync-no-mod-load"):
        overrides += NO_MOD_LOAD
    if args.mode == "sync-no-mod-load":
        overrides += SYNC_STARTUP
    if args.mode in ("scoped-load", "trace-load"):
        overrides = SCOPED_OVERRIDES
    if args.mode == "trace-load":
        overrides += "\n" + (Path(__file__).resolve().parent / "patches/server_probe.lua").read_text(encoding="utf-8")
    if args.mode == "vanilla":
        shutil.copyfile(romfs / "scripts" / "globalvariableoverrides.lua",
                        scripts / "globalvariableoverrides.lua")
    else:
        (scripts / "globalvariableoverrides.lua").write_text(overrides, encoding="utf-8")

    names = [m.strip() for m in args.mods.split(",") if m.strip()]
    mods_dir.mkdir(parents=True, exist_ok=True)
    (mods_dir / "modsettings.lua").write_text(MODSETTINGS % names[0], encoding="utf-8")

    for name in names:
        d = mods_dir / name
        d.mkdir(parents=True)
        (d / "modinfo.lua").write_text(MODINFO, encoding="utf-8")
        modmain = MODMAIN.replace("DST-MOD R9", "DST-MOD R10") if args.mode == "trace-load" else MODMAIN
        (d / "modmain.lua").write_text(modmain, encoding="utf-8")

    n = len(list(root.rglob("*")))
    print("已生成：%s（%d 个条目，模式 %s）" % (root, n, args.mode))
    for p in sorted(root.rglob("*")):
        if p.is_file():
            print("  %8d  %s" % (p.stat().st_size, p.relative_to(root)))


if __name__ == "__main__":
    main()
