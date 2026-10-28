#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum SlTaskKind { SlTask_Upload, SlTask_Install };
enum SlTaskStage { SlTask_Transferring, SlTask_Installing, SlTask_Registering, SlTask_Completed, SlTask_Failed, SlTask_Cancelled };
typedef struct SlTaskProgress {
    char id[97];
    char name[513];
    char wire_name[609];
    enum SlTaskKind kind;
    enum SlTaskStage stage;
    uint64_t bytes_done;
    uint64_t bytes_total;
    double speed;
} SlTaskProgress;
bool sl_parse_task_progress(const char* json, size_t size, const char* device_id, SlTaskProgress* out);
bool sl_merge_task_progress(SlTaskProgress* current, const SlTaskProgress* incoming);
bool sl_task_terminal(enum SlTaskStage stage);
const char* sl_task_stage_name(enum SlTaskStage stage);
double sl_task_percent(const SlTaskProgress* task);
#ifdef __cplusplus
}
#endif
