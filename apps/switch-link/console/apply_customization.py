"""Apply Switch Link's small, reproducible overlay to the pinned GPL source."""
from pathlib import Path
import shutil, sys

ROOT = Path(__file__).resolve().parent
source = Path(sys.argv[1]).resolve()
def change(name, old, new):
    path = source / name
    text = path.read_text(encoding='utf-8')
    if old not in text:
        raise RuntimeError(f'Expected upstream source is missing: {name}: {old[:90]}')
    path.write_text(text.replace(old, new), encoding='utf-8', newline='\n')

change('CMakeLists.txt', 'project(sphaira LANGUAGES', 'project(SwitchLink LANGUAGES')
change('sphaira/CMakeLists.txt', 'set(sphaira_VERSION 1.0.7)', 'set(sphaira_VERSION 0.2.3)')
change('sphaira/CMakeLists.txt', '    source/ftpsrv_helper.cpp', '    source/switch_link.cpp\n        source/task_progress.c\n        source/http_install.c\n        source/ftpsrv_helper.cpp')
change('sphaira/CMakeLists.txt', 'NAME ${CMAKE_PROJECT_NAME}', 'NAME "Switch Link 0.2.3"')
change('sphaira/CMakeLists.txt', 'AUTHOR TotalJustice', 'AUTHOR "Switch Link / Sphaira contributors"')
change('sphaira/source/main.cpp', '#include "ui/menus/main_menu.hpp"', '#include "ui/menus/main_menu.hpp"\n#include "ui/menus/ftp_menu.hpp"')
change('sphaira/source/main.cpp', 'app->Push<sphaira::ui::menu::main::MainMenu>();', 'app->Push<sphaira::ui::menu::ftp::Menu>(0);')

# Keep application config/cache paths separate from any existing Sphaira setup.
for path in list((source/'sphaira').rglob('*.cpp')) + list((source/'sphaira').rglob('*.hpp')):
    original = path.read_text(encoding='utf-8')
    data = original.replace('/config/sphaira', '/config/switch-link').replace('/switch/sphaira', '/switch/switch-link')
    if data != original:
        path.write_text(data, encoding='utf-8', newline='\n')
change('sphaira/include/app.hpp', '"ftp_enabled", false', '"ftp_enabled", true')
change('sphaira/include/app.hpp', '"hdd_enabled", true', '"hdd_enabled", false')
change('sphaira/include/app.hpp', '"theme_music", true', '"theme_music", false')
change('sphaira/include/app.hpp', '"language", 0', '"language", 7')
change('sphaira/include/app.hpp', '"install_emummc", false', '"install_emummc", true')
change('sphaira/include/app.hpp', '"show_album", true', '"show_album", false')
change('sphaira/include/app.hpp', '"show_games", true', '"show_games", false')

change('sphaira/source/app.cpp', '#include "app.hpp"', '#include "app.hpp"\n#include "switch_link.hpp"')
change('sphaira/source/app.cpp', 'void App::Update() {', 'void App::Update() {\n#ifdef ENABLE_FTPSRV\n    if (App::GetFtpEnable()) switch_link::UiTick(ftpsrv::GetPort());\n#endif')
change('sphaira/source/app.cpp', 'App::~App() {', 'App::~App() {\n    switch_link::BeaconClose();')
change('sphaira/source/ui/menus/install_stream_menu_base.cpp', '#include "yati/yati.hpp"', '#include "yati/yati.hpp"\n#include "switch_link.hpp"')
change('sphaira/source/ui/menus/install_stream_menu_base.cpp', 'const auto rc = yati::InstallFromSource(pbox, source.get(), source->GetPath());', 'switch_link::BeginInstallReceipt(source->GetPath().s);\n        const auto rc = yati::InstallFromSource(pbox, source.get(), source->GetPath());\n        switch_link::WriteInstallReceipt(source->GetPath().s, rc);')
change('sphaira/source/yati/yati.cpp', '#include "yati/yati.hpp"', '#include "yati/yati.hpp"\n#include "switch_link.hpp"')
change('sphaira/source/yati/yati.cpp', 'log_write("pushed\\n");', 'log_write("pushed\\n");\n    switch_link::RecordRegisteredTitle();')
change('sphaira/source/yati/yati.cpp', 'Result Yati::RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num) {', 'Result Yati::RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num) {\n    switch_link::MarkRegistering();')

for name in ['switch_link.cpp', 'task_progress.c', 'http_install.c', 'ui/menus/ftp_menu.cpp', 'ui/progress_box.cpp']:
    shutil.copyfile(ROOT / 'overlay/source' / name, source / 'sphaira/source' / name)
shutil.copyfile(ROOT / 'overlay/include/switch_link.hpp', source / 'sphaira/include/switch_link.hpp')
shutil.copyfile(ROOT / 'overlay/include/task_progress.h', source / 'sphaira/include/task_progress.h')
shutil.copyfile(ROOT / 'overlay/include/http_install.h', source / 'sphaira/include/http_install.h')
shutil.copyfile(ROOT / 'overlay/include/ui/menus/ftp_menu.hpp', source / 'sphaira/include/ui/menus/ftp_menu.hpp')
shutil.copyfile(ROOT / 'overlay/default_theme.ini', source / 'assets/romfs/themes/default_theme.ini')
shutil.copyfile(ROOT / 'overlay/icon.jpg', source / 'assets/icon.jpg')
print('Applied Switch Link console overlay.')

# Capability-bound native installs must not follow redirects to another origin.
change('sphaira/include/yati/source/http.hpp', 'std::stop_token token);', 'std::stop_token token, bool allow_redirects = true, s64 source_size = -1);')
change('sphaira/include/yati/source/http.hpp', '    const std::stop_token m_token;', '    const std::stop_token m_token;\n    const bool m_allow_redirects;\n    const s64 m_source_size;')
change('sphaira/source/yati/source/http.cpp', 'std::stop_token token)\n: m_url{url}, m_user{user}, m_pass{pass}, m_token{token}', 'std::stop_token token, bool allow_redirects, s64 source_size)\n: m_url{url}, m_user{user}, m_pass{pass}, m_token{token}, m_allow_redirects{allow_redirects}, m_source_size{source_size}')
change('sphaira/source/yati/source/http.cpp', 'CURLOPT_FOLLOWLOCATION, 1L', 'CURLOPT_FOLLOWLOCATION, m_allow_redirects ? 1L : 0L')
change('sphaira/source/yati/source/http.cpp', '    *bytes_read = 0;', '    *bytes_read = 0;\n    R_UNLESS(off >= 0 && size >= 0 && (m_source_size < 0 || (off <= m_source_size && size <= m_source_size - off)), Result_YatiHttpReadFailed);')
