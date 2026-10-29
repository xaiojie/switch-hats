-- 不走 Mods 界面，直接在启动时强制启用测试 mod
-- （Mods 页在主机版会崩：TheSim:QueryTopMods 不存在、还有别的 PC 专有调用）
ForceEnableMod("dst-test-mod")
DisableModDisabling()
DisableLocalModWarning()
