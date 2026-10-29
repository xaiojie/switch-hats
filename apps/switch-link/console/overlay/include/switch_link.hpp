#pragma once
#include <switch.h>
#include "task_progress.h"
#include "http_install.h"
namespace sphaira::switch_link {
struct TaskSnapshot {
    SlTaskProgress task{};
    char step[257]{};
    bool visible{};
    bool native_active{};
    bool native_terminal{};
    bool stale{};
};
void UiTick(unsigned port);
void BeaconTick(unsigned port);
void BeaconClose();
TaskSnapshot GetTaskSnapshot();
void ReportInstallerProgress(const char* step, s64 offset, s64 size, s64 speed);
void SetHttpReceiverReady(bool ready);
bool TakeHttpInstall(SlHttpInstall& command);
bool PersistHttpInstall(const SlHttpInstall& command);
bool HttpInstallSucceeded(Result result);
void PostHttpInstallResult(const SlHttpInstall& command, Result result);
void FinishHttpInstall();
void BeginInstallReceipt(const char* path);
void MarkRegistering();
void RecordRegisteredTitle();
bool WriteInstallReceipt(const char* path, Result result);
}
