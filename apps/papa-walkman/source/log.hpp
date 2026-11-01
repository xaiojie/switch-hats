#pragma once
#include <cstdarg>

namespace papa {

// 真机诊断日志：同时走两条互相独立的通道
//   1) libnx fs 句柄（不经过 newlib 的 fd 表）→ sdmc:/config/papa-walkman/trace.txt
//   2) newlib stdio（可能踩到 fd 表的问题）→ sdmc:/config/papa-walkman/log.txt
// 两条通道都写同一行内容，stdio 通道崩掉时 trace.txt 仍能留下最后状态。
// Logf 只向有上限的队列提交；后台线程批量写两条通道。队列忙或满会丢弃
// 诊断消息并记录省略数量，不等待 SD。主机构建输出 stderr，不产生文件。
void LogInit();
void LogClose();
void Logf(const char *format, ...) __attribute__((format(printf, 1, 2)));

// 打印日志文件句柄在 newlib fd 表里的状态（fd / device / devoptab），用于定位
// “devoptab_list[device] 变成 NULL” 的时机。
void LogProbe(const char *tag);

// 打印整张 devoptab 表（索引:名字）。
void LogTable(const char *tag);

// 保留旧名称：启动日志写入线程，按 100ms 或 16 行批量刷新。
// LogInit 已自动调用；重复调用安全。不再读取 newlib 全局内存快照。
// 用 libnx threadCreate 并检查 Result，不使用 detach。
void LogStartWatchdog();

// 安装 std::terminate / atexit 钩子。静默退出（无崩溃报告）时用来区分
// “未捕获 C++ 异常”与“正常 exit/main 返回”。
void LogInstallExitHooks();

// 濒死路径专用：只写 fs 通道（不抢锁、不碰 stdio）。
void TraceRawLine(const char *text);

} // namespace papa
