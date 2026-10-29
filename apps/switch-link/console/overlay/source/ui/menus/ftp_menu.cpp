#include "ui/menus/ftp_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "ftpsrv_helper.hpp"
#include "switch_link.hpp"
#include "ui/progress_box.hpp"
#include "yati/yati.hpp"
#include "yati/source/http.hpp"
#include "utils/utils.hpp"
#include <cmath>
#include <algorithm>

namespace sphaira::ui::menu::ftp {

Menu::Menu(u32 flags) : stream::Menu{"Switch Link 0.2.3", flags} {
    m_was_ftp_enabled = App::GetFtpEnable();
    if (!m_was_ftp_enabled) App::SetFtpEnable(true);
    // FTP manages SD files; only HTTP commands may start installs in 0.2.3.
    ftpsrv::DisableInstallMode();
    m_port = ftpsrv::GetPort();
    m_anon = ftpsrv::IsAnon();
    RemoveAction(Button::X);
    SetAction(Button::B, Action{"退出", [](){ App::Exit(); }});
    SetAction(Button::A, Action{"浏览 SD 卡", [](){
        App::Push<filebrowser::Menu>(0, filebrowser::FsOption_CanInstall | filebrowser::FsOption_CanSelect | filebrowser::FsOption_DoNotPrompt);
    }});
}

Menu::~Menu() {
    switch_link::SetHttpReceiverReady(false);
    CancelInstallMode();
    if (!m_was_ftp_enabled) App::SetFtpEnable(false);
}

void Menu::OnFocusGained() {
    stream::Menu::OnFocusGained();
    switch_link::SetHttpReceiverReady(true);
}
void Menu::OnFocusLost() {
    switch_link::SetHttpReceiverReady(false);
    stream::Menu::OnFocusLost();
}
void Menu::Update(Controller* controller, TouchInfo* touch) {
    stream::Menu::Update(controller, touch);
    // A controller action may have pushed the file browser during Update.
    if (!HasFocus() || ShouldPop()) return;
    SlHttpInstall command{};
    if (!switch_link::TakeHttpInstall(command)) return;
    App::Push<ui::ProgressBox>(0, "正在通过 HTTP 安装", command.name, [command](auto pbox) -> Result {
        const bool persisted = switch_link::PersistHttpInstall(command);
        switch_link::BeginInstallReceipt(command.wire_name);
        Result result = MAKERESULT(Module_Libnx, LibnxError_IoError);
        if (persisted) {
            yati::source::Http source{command.url, "", "", pbox->GetToken(), false, static_cast<s64>(command.size)};
            yati::ConfigOverride config{};
            config.sd_card_install = true;
            result = yati::InstallFromSource(pbox, &source, command.wire_name, config, static_cast<s64>(command.size));
        }
        switch_link::WriteInstallReceipt(command.wire_name, result);
        switch_link::PostHttpInstallResult(command, result);
        const bool success = switch_link::HttpInstallSucceeded(result);
        switch_link::FinishHttpInstall();
        // Result 0 without application registration also fails the console UI.
        return success ? 0 : R_FAILED(result) ? result : MAKERESULT(Module_Libnx, LibnxError_IoError);
    }, [](Result result) {
        if (R_FAILED(result)) App::PushErrorBox(result, "HTTP 安装失败");
        else App::Notify("安装完成 · 主机已注册应用记录");
    }, ui::ProgressBoxOption::ScreenToggle);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    // The upstream progress box remains on top while installing.
    MenuBase::Draw(vg, theme);
    const auto pdata = GetPolledData();
    const auto text = nvgRGB(241, 246, 255);
    const auto muted = nvgRGB(151, 170, 197);
    const auto accent = nvgRGB(101, 233, 207);
    const auto card = nvgRGB(20, 29, 45);
    const auto snapshot = switch_link::GetTaskSnapshot();
    const auto info = [&](float x, float y, float size, NVGcolor colour, const char* value) {
        gfx::drawText(vg, x, y, size, colour, value, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    };

    gfx::drawRect(vg, 55, 122, 1170, 145, card, 22);
    info(86, 147, 15, accent, "S W I T C H   L I N K");
    info(86, 181, 36, text, snapshot.visible ? "正在显示当前任务进度" : pdata.ip ? "已就绪，电脑会自动发现这台 Switch" : "等待连接局域网");
    info(86, 234, 18, muted, "电脑打开 Switch Link · 保持此页面接收安装任务 · 无需反复输入 IP");

    if (snapshot.visible) {
        const auto& task = snapshot.task;
        const bool terminal = sl_task_terminal(task.stage);
        const char* stage = "正在传输到 SD 卡";
        switch (task.stage) {
            case SlTask_Transferring: stage = task.kind == SlTask_Install ? "正在传输安装包" : "正在传输到 SD 卡"; break;
            case SlTask_Installing: stage = "正在安装 · 当前步骤"; break;
            case SlTask_Registering: stage = "正在提交应用记录 · 尚未完成"; break;
            case SlTask_Completed: stage = task.kind == SlTask_Install ? "安装完成 · 主机已注册应用记录" : "文件传输完成"; break;
            case SlTask_Failed: stage = "任务失败 · 请查看电脑端错误信息"; break;
            case SlTask_Cancelled: stage = "任务已取消"; break;
        }
        const auto colour = task.stage == SlTask_Failed || task.stage == SlTask_Cancelled ? nvgRGB(255, 99, 122) : accent;
        gfx::drawRect(vg, 55, 288, 1170, 321, card, 22);
        info(86, 315, 22, colour, stage);
        nvgSave(vg);
        nvgIntersectScissor(vg, 86, 355, 1108, 42);
        info(86, 355, 28, text, task.name);
        nvgRestore(vg);
        double percent = task.stage == SlTask_Completed ? 100 : sl_task_percent(&task);
        if (task.kind == SlTask_Install && !snapshot.native_active && !terminal && task.stage != SlTask_Transferring) percent = -1;
        char value[160];
        if (percent >= 0) {
            snprintf(value, sizeof(value), "%.1f%%", percent);
            gfx::drawText(vg, 1190, 410, 48, colour, value, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        } else {
            gfx::drawSpinner(vg, theme, 1165, 440, 19, armTicksToNs(armGetSystemTick()) / 1e+9);
        }
        if (task.bytes_total && percent >= 0) {
            snprintf(value, sizeof(value), "%s / %s", utils::formatSizeStorage(task.bytes_done).c_str(), utils::formatSizeStorage(task.bytes_total).c_str());
            info(86, 425, 23, text, value);
        } else {
            info(86, 425, 22, muted, "正在处理当前安装步骤，请稍候");
        }
        if (task.speed > 0 && !terminal) {
            const auto speed = utils::formatSizeNetwork(task.speed);
            snprintf(value, sizeof(value), "速度 %s/s", speed.c_str());
            info(86, 463, 20, muted, value);
        }
        gfx::drawRect(vg, 86, 505, 1108, 14, nvgRGB(33, 46, 66), 7);
        if (percent >= 0) gfx::drawRect(vg, 86, 505, 1108 * std::clamp(percent / 100.0, 0.0, 1.0), 14, colour, 7);
        if (snapshot.stale) {
            snprintf(value, sizeof(value), "等待电脑更新进度 · 请勿退出应用");
        } else if (!terminal && percent >= 0 && task.speed > 0 && task.bytes_total > task.bytes_done) {
            const auto seconds = static_cast<u64>(std::ceil((task.bytes_total - task.bytes_done) / task.speed));
            snprintf(value, sizeof(value), "预计剩余 %llu 分 %02llu 秒", static_cast<unsigned long long>(seconds / 60), static_cast<unsigned long long>(seconds % 60));
        } else if (task.stage == SlTask_Completed) {
            snprintf(value, sizeof(value), "主机已完成此任务");
        } else if (task.kind == SlTask_Install && !terminal) {
            snprintf(value, sizeof(value), "安装未完成，请保持运行；完成结果由主机回传");
        } else {
            snprintf(value, sizeof(value), "请保持 Switch Link 运行，电脑端可查看任务详情");
        }
        info(86, 543, 19, muted, value);
        if (snapshot.step[0]) {
            nvgSave(vg);
            nvgIntersectScissor(vg, 86, 578, 1108, 25);
            info(86, 578, 16, muted, snapshot.step);
            nvgRestore(vg);
        }
        info(58, 630, 16, muted, "传输或安装中请勿退出。安装百分比表示当前步骤，应用注册完成才算安装完成。");
        return;
    }

    gfx::drawRect(vg, 55, 288, 574, 230, card, 22);
    gfx::drawRect(vg, 651, 288, 574, 230, card, 22);
    info(86, 316, 15, accent, "01  /  FILES");
    info(86, 350, 31, text, "SD 卡文件");
    info(86, 399, 20, muted, "在电脑上浏览、上传和下载文件");
    info(86, 441, 18, muted, "按 A 也可在主机上浏览 SD 卡");
    info(682, 316, 15, accent, "02  /  INSTALL");
    info(682, 350, 31, text, "游戏安装");
    info(682, 399, 20, muted, "在电脑端选择本地安装文件");
    info(682, 441, 18, muted, "支持 NSP / XCI · 通过 HTTP 安装");
    info(682, 477, 16, muted, "安装结果由主机完成后回传");

    gfx::drawRect(vg, 55, 540, 1170, 77, nvgRGB(13, 38, 39), 18);
    char status[200];
    if (pdata.ip) {
        snprintf(status, sizeof(status), "局域网已连接  ·  %u.%u.%u.%u:%u  ·  %s",
            pdata.ip & 0xFF, (pdata.ip >> 8) & 0xFF, (pdata.ip >> 16) & 0xFF, (pdata.ip >> 24) & 0xFF,
            m_port, HasFocus() ? "HTTP 安装已就绪" : "返回接收页面后可安装");
    } else {
        snprintf(status, sizeof(status), "请让电脑与 Switch 连接同一个 Wi-Fi 或有线局域网");
    }
    info(86, 567, 21, accent, status);
    info(58, 630, 16, muted, "安装时请保持运行。浏览主机文件后，按 B 返回此接收页面。");
}

void Menu::OnDisableInstallMode() { ftpsrv::DisableInstallMode(); }
bool Menu::IsInstallModeActive() const { return ftpsrv::IsInstallModeEnabled(); }
}
