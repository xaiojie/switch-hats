-- 最小验证 mod：主机模拟中的玩家出生时给一块金块
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
