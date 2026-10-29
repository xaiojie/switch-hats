-- R8 diagnostics: in-memory phase reporting between the game's Lua instances.
-- No save files, filesystem writes, or changes to the server startup decisions.
do
    local records = {}
    local function receive(message)
        local id, stage, detail = tostring(message):match("^(%-?%d+)|([^|]*)|(.*)$")
        if id then
            records[tonumber(id)] = {stage = stage, detail = detail}
        end
    end
    rawset(_G, "DSTModProbeReceive", receive)

    local function trace(stage, detail)
        local sim = rawget(_G, "TheSim")
        local id = sim and sim.GetGameIndex and sim:GetGameIndex() or -1
        detail = tostring(detail or ""):gsub("[%c|]", " "):sub(1, 110)
        local message = tostring(id).."|"..stage.."|"..detail
        receive(message)
        print("[DST R8] "..message)
        if sim and sim.SendCommandToInstance then
            for _, target in ipairs({0, 4}) do
                if target ~= id then
                    pcall(sim.SendCommandToInstance, sim, target, "DSTModProbeReceive", message)
                end
            end
        end
    end

    local function pack(...)
        return {n = select("#", ...), ...}
    end

    local wrapped = {}
    local function wrap(owner, key, label)
        if owner == nil or type(owner[key]) ~= "function" then return end
        local original = owner[key]
        if wrapped[original] then return end
        local replacement = function(...)
            trace(label..":enter")
            local result = pack(pcall(original, ...))
            if not result[1] then
                trace(label..":error", result[2])
                error(result[2], 0)
            end
            trace(label..":done", result[2])
            return unpack(result, 2, result.n)
        end
        wrapped[replacement] = true
        owner[key] = replacement
    end

    wrap(ModManager, "LoadMods", "load_mods")
    wrap(ModManager, "LoadServerModsFile", "server_mod_setup")
    wrap(ModManager, "InitializeModMain", "modmain")
    wrap(ModManager, "SimPostInit", "sim_postinit")
    wrap(KnownModIndex, "UpdateModSettings", "mod_settings")
    wrap(KnownModIndex, "UpdateModInfo", "mod_info")
    wrap(KnownModIndex, "LoadModOverides", "mod_overrides")
    wrap(KnownModIndex, "LoadModConfigurationOptions", "mod_config")
    wrap(rawget(_G, "TheNet"), "BeginServerModSetup", "begin_mod_setup")
    wrap(rawget(_G, "TheNet"), "DownloadServerMods", "download_mods")
    wrap(rawget(_G, "TheNet"), "StartServer", "start_server")
    wrap(rawget(_G, "TheSystemService"), "StartDedicatedServers", "launch_servers")

    local sim = rawget(_G, "TheSim")
    if sim and sim.GenerateNewWorld then
        local original_generate = sim.GenerateNewWorld
        sim.GenerateNewWorld = function(self, leveldata, moddata, callback, ...)
            trace("worldgen:enter")
            local function done(data, ...)
                local detail = type(data) == "string" and
                    (data:sub(1, 5) == "error" and data or "bytes="..#data) or type(data)
                trace("worldgen:callback", detail)
                return callback(data, ...)
            end
            return original_generate(self, leveldata, moddata, done, ...)
        end
    end

    local function hook_globals()
        for _, name in ipairs({"StartDedicatedServer", "StartNextInstance", "DoInitGame"}) do
            if type(rawget(_G, name)) == "function" then
                wrap(_G, name, name)
            end
        end
    end
    hook_globals()

    local function describe(id)
        local row = records[id]
        if row == nil then return "S"..id..": no report" end
        return "S"..id..": "..row.stage..
            (row.detail ~= "" and " "..row.detail or "")
    end

    local function hook_popup(class)
        if type(class) ~= "table" or class.__dst_probe_wrapped or
           type(class.OnUpdate) ~= "function" then return end
        class.__dst_probe_wrapped = true
        local original_update = class.OnUpdate
        class.OnUpdate = function(self, dt)
            local result = pack(original_update(self, dt))
            local status = TheNet:GetChildProcessStatus()
            if status ~= 3 and self.text and self.title then
                local elapsed = math.floor(GetTime() - (self.launchtime or GetTime()))
                self.title:SetSize(22)
                self.title:SetString("DST R8 state="..tostring(status).." / "..elapsed.."s")
                local state = status == 2 and "Generating world" or "Starting server"
                self.text:SetSize(20)
                self.text:SetRegionSize(440, 140)
                self.text:SetString(state.."\n"..describe(2).."\n"..describe(3).."\n"..describe(0))
            end
            return unpack(result, 1, result.n)
        end
    end

    hook_popup(package.loaded["screens/launchingserverpopup"])
    local original_require = require
    require = function(name)
        local result = original_require(name)
        if name == "screens/launchingserverpopup" then hook_popup(result) end
        if name == "networking" or name == "mainfunctions" or name == "gamelogic" then
            hook_globals()
        end
        return result
    end
    local net = rawget(_G, "TheNet")
    trace("probe:ready", net and net.IsDedicated and ("dedicated="..tostring(net:IsDedicated())) or "")
end
