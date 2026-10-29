import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from lupa.lua51 import LuaRuntime
from lupa.lua54 import LuaRuntime as LuaWithoutLegacyArg
import build_overlay


class DiagnosticOverlayTests(unittest.TestCase):
    def generate(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "romfs/scripts").mkdir(parents=True)
            (root / "romfs/scripts/modindex.lua").write_text("-- fixture")
            result = subprocess.run([
                sys.executable, str(Path(__file__).with_name("build_overlay.py")),
                "--romfs", str(root / "romfs"), "--out", str(root / "out"),
                "--mode", mode,
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            overlay = root / "out/atmosphere/contents/010090100E334000/romfs/scripts/globalvariableoverrides.lua"
            return overlay.read_text()

    def runtime(self):
        lua = LuaRuntime()
        lua.execute('''
                calls = 0
                async_reads = 0
                settings_loaded = false
                TheSim = {}
                package.assetpath = {{path = ""}}
                ModManager = {LoadMods = function() calls = calls + 1 end}
                local runmodfn = function(fn) return fn end
                ModManager.GetPostInitFns = function()
                    return {runmodfn(function() end)}
                end
                KnownModIndex = {
                    forceddirs = {},
                    UpdateModInfo = function() end,
                    UpdateModSettings = function() settings_loaded = true end,
                    Load = function(self, callback) async_reads = async_reads + 1 end,
                    BeginStartupSequence = function(self, callback) async_reads = async_reads + 1 end,
                }
                require = function() return true end
            ''')
        return lua

    def test_no_mod_load_keeps_switch_enabled_but_never_calls_loader(self):
        lua = self.runtime()
        lua.execute(self.generate("no-mod-load"))
        lua.execute('ModManager:LoadMods(); require("mods"); ModManager:LoadMods()')
        self.assertEqual(lua.globals().calls, 0)
        self.assertTrue(lua.globals().MODS_ENABLED)

    def test_sync_probe_initializes_before_native_render_can_resume(self):
        lua = self.runtime()
        lua.execute(self.generate("sync-no-mod-load"))
        lua.execute('''
            initialized = false
            KnownModIndex:Load(function()
                KnownModIndex:BeginStartupSequence(function()
                    ModManager:LoadMods()
                    initialized = true
                end)
            end)
        ''')
        self.assertTrue(lua.globals().initialized)
        self.assertTrue(lua.globals().settings_loaded)
        self.assertTrue(lua.globals().KnownModIndex.startingup)
        self.assertEqual(lua.globals().async_reads, 0)
        self.assertEqual(lua.globals().calls, 0)

    def test_scoped_loader_is_installed_immediately_and_restores_console_flag(self):
        lua = self.runtime()
        lua.execute('''
            ModManager.LoadMods = function()
                assert(MODS_ENABLED)
                calls = calls + 1
                KnownModIndex:UpdateModInfo()
                table.insert(package.assetpath, {path="mods/dst-test-mod/"})
            end
        ''')
        lua.execute(self.generate("scoped-load"))
        self.assertFalse(lua.globals().MODS_ENABLED)
        lua.execute('ModManager:LoadMods()')
        self.assertEqual(lua.globals().calls, 1)
        self.assertTrue(lua.globals().settings_loaded)
        self.assertTrue(lua.globals().KnownModIndex.forceddirs['dst-test-mod'])
        self.assertFalse(lua.globals().MODS_ENABLED)
        self.assertEqual(lua.eval('#package.assetpath'), 1)

    def test_scoped_loader_restores_console_flag_even_on_lua_error(self):
        lua = self.runtime()
        lua.execute('ModManager.LoadMods = function() error("loader failed") end')
        lua.execute(self.generate("scoped-load"))
        lua.execute('ok, err = pcall(function() ModManager:LoadMods() end)')
        self.assertFalse(lua.globals().ok)
        self.assertIn('loader failed', lua.globals().err)
        self.assertFalse(lua.globals().MODS_ENABLED)

    def test_mod_uses_global_spawn_and_only_grants_on_master_sim(self):
        for ismaster, expected in ((True, 1), (False, 0)):
            with self.subTest(ismaster=ismaster):
                lua = LuaRuntime()
                lua.execute('''
                    count = 0
                    GLOBAL = {
                        TheWorld = {ismastersim = true},
                        SpawnPrefab = function(name)
                            assert(name == "goldnugget")
                            return {prefab = name}
                        end,
                        STRINGS = {UI = {MAINSCREEN = {CREATE = "Create"}}},
                    }
                    AddPlayerPostInit = function(fn) postinit = fn end
                    inst = {
                        IsValid = function() return true end,
                        DoTaskInTime = function(self, delay, fn) fn() end,
                        components = {inventory = {
                            GiveItem = function(self, item)
                                assert(item.prefab == "goldnugget")
                                count = count + 1
                            end,
                        }},
                    }
                ''')
                lua.globals().GLOBAL.TheWorld.ismastersim = ismaster
                lua.execute(build_overlay.MODMAIN)
                lua.execute('postinit(inst)')
                self.assertEqual(lua.globals().count, expected)

    def test_callback_bridge_does_not_require_legacy_arg_and_keeps_nil_arguments(self):
        # Switch's Lua lacks the legacy implicit `arg` table. Lua 5.4 models
        # that difference here; the emitted patch itself remains Lua 5.1 syntax.
        lua = LuaWithoutLegacyArg()
        lua.execute('''
            unpack = table.unpack
            TheSim = {}
            package.assetpath = {{path = ""}}
            KnownModIndex = {UpdateModInfo = function() end}
            ModManager = {LoadMods = function() end}
            require = function() return true end
            local runmodfn = function(fn, mod, modtype)
                return function(...)
                    return fn(unpack(arg))
                end
            end
            ModManager.GetPostInitFns = function(self, fn)
                return {runmodfn(fn, {modname="dst-test-mod"}, "PrefabPostInitAny")}
            end
        ''')
        lua.execute(self.generate("scoped-load"))
        lua.execute('''
            observed = nil
            local callbacks = ModManager:GetPostInitFns(function(...)
                observed = {n=select("#", ...), ...}
                return "called"
            end)
            result = callbacks[1]("player", nil, "tail", nil)
        ''')
        self.assertEqual(lua.globals().result, "called")
        self.assertEqual(lua.globals().observed.n, 4)
        self.assertEqual(lua.globals().observed[1], "player")
        self.assertIsNone(lua.globals().observed[2])
        self.assertEqual(lua.globals().observed[3], "tail")

    def test_trace_overlay_displays_native_status_and_remote_server_error(self):
        lua = self.runtime()
        lua.execute('''
            now = 120
            GetTime = function() return now end
            TheSim.GetGameIndex = function() return 0 end
            TheSim.SendCommandToInstance = function() end
            TheNet = {
                GetChildProcessStatus = function() return 1 end,
                GetChildProcessError = function() return false end,
            }
            Popup = {OnUpdate = function(self, dt) self.updated = true end}
            package.loaded["screens/launchingserverpopup"] = Popup
            require = function(name) return package.loaded[name] or true end
            local function widget()
                return {
                    SetString = function(self, value) self.value = value end,
                    SetSize = function() end,
                    SetRegionSize = function() end,
                }
            end
            popup = {launchtime=0, title=widget(), text=widget()}
        ''')
        lua.execute(self.generate("trace-load"))
        lua.execute('DSTModProbeReceive("2|load_mods:error|missing server API"); Popup.OnUpdate(popup, 0.1)')
        self.assertTrue(lua.globals().popup.updated)
        self.assertIn("state=1", lua.globals().popup.title.value)
        self.assertIn("120s", lua.globals().popup.title.value)
        self.assertIn("load_mods:error", lua.globals().popup.text.value)
        self.assertIn("missing server API", lua.globals().popup.text.value)


if __name__ == "__main__":
    unittest.main()
