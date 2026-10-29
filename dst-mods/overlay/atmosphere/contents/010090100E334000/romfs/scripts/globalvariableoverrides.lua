-- DST Switch R9: keep the console startup/menu path.
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
