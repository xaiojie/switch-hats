#include "task_progress.h"
#include <yyjson.h>
#include <math.h>
#include <string.h>

static bool copy_string(yyjson_val* obj, const char* key, char* dest, size_t capacity, bool required) {
    yyjson_val* value = yyjson_obj_get(obj, key);
    if (!value && !required) { dest[0] = 0; return true; }
    if (!yyjson_is_str(value)) return false;
    size_t size = yyjson_get_len(value);
    const char* text = yyjson_get_str(value);
    if (!size) { dest[0] = 0; return !required; }
    if (size >= capacity || strlen(text) != size) return false;
    for (size_t i = 0; i < size; i++) if ((unsigned char)text[i] < 32 || text[i] == 127) return false;
    memcpy(dest, text, size + 1);
    return true;
}

const char* sl_task_stage_name(enum SlTaskStage stage) {
    static const char* const names[] = {"transferring", "installing", "registering", "completed", "failed", "cancelled"};
    return stage >= SlTask_Transferring && stage <= SlTask_Cancelled ? names[stage] : "failed";
}

bool sl_task_terminal(enum SlTaskStage stage) { return stage >= SlTask_Completed; }

double sl_task_percent(const SlTaskProgress* task) {
    if (!task || !task->bytes_total) return -1;
    return (double)task->bytes_done / (double)task->bytes_total * 100.0;
}

bool sl_parse_task_progress(const char* json, size_t size, const char* device_id, SlTaskProgress* out) {
    if (!json || !size || size > 2048 || !device_id || !out) return false;
    yyjson_doc* doc = yyjson_read(json, size, YYJSON_READ_NOFLAG);
    if (!doc) return false;
    yyjson_val* obj = yyjson_doc_get_root(doc);
    SlTaskProgress value = {0};
    char type[32], device[33], kind[16], stage[17];
    bool valid = yyjson_is_obj(obj)
        && copy_string(obj, "type", type, sizeof(type), true) && !strcmp(type, "switch-link-task")
        && yyjson_is_uint(yyjson_obj_get(obj, "version")) && yyjson_get_uint(yyjson_obj_get(obj, "version")) == 1
        && copy_string(obj, "device_id", device, sizeof(device), true) && !strcmp(device, device_id)
        && copy_string(obj, "id", value.id, sizeof(value.id), true)
        && copy_string(obj, "name", value.name, sizeof(value.name), true)
        && copy_string(obj, "wire_name", value.wire_name, sizeof(value.wire_name), false)
        && copy_string(obj, "kind", kind, sizeof(kind), true)
        && copy_string(obj, "stage", stage, sizeof(stage), true);
    if (valid) {
        if (!strcmp(kind, "install")) value.kind = SlTask_Install;
        else if (!strcmp(kind, "upload")) value.kind = SlTask_Upload;
        else valid = false;
        bool found = false;
        for (int i = SlTask_Transferring; i <= SlTask_Cancelled; i++) {
            if (!strcmp(stage, sl_task_stage_name(i))) { value.stage = i; found = true; break; }
        }
        valid &= found;
        for (size_t i = 0; value.id[i]; i++) {
            char ch = value.id[i];
            valid &= (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        }
    }
    yyjson_val* done = yyjson_obj_get(obj, "bytes_done");
    yyjson_val* total = yyjson_obj_get(obj, "bytes_total");
    yyjson_val* speed = yyjson_obj_get(obj, "speed");
    yyjson_val* progress = yyjson_obj_get(obj, "progress");
    valid = valid && yyjson_is_uint(done) && yyjson_is_uint(total) && yyjson_is_num(speed);
    if (valid) {
        value.bytes_done = yyjson_get_uint(done);
        value.bytes_total = yyjson_get_uint(total);
        value.speed = yyjson_get_num(speed);
        valid = value.bytes_done <= value.bytes_total && value.bytes_total <= (16ULL << 40)
            && isfinite(value.speed) && value.speed >= 0 && value.speed <= (8ULL << 30);
        if (progress && !yyjson_is_null(progress)) {
            double percent = yyjson_get_num(progress);
            valid &= yyjson_is_num(progress) && isfinite(percent) && percent >= 0 && percent <= 100;
        }
    }
    if (valid) *out = value;
    yyjson_doc_free(doc);
    return valid;
}

bool sl_merge_task_progress(SlTaskProgress* current, const SlTaskProgress* incoming) {
    if (!strcmp(current->id, incoming->id)) {
        if (sl_task_terminal(current->stage) && !sl_task_terminal(incoming->stage)) return false;
        if (current->stage == SlTask_Transferring && incoming->stage == SlTask_Transferring
            && (incoming->bytes_done < current->bytes_done || (current->bytes_total && incoming->bytes_total != current->bytes_total))) return false;
    }
    *current = *incoming;
    return true;
}
