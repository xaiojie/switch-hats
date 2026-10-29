#include "task_progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* device = "0123456789abcdef0123456789abcdef";
static const char* good = "{\"type\":\"switch-link-task\",\"version\":1,\"device_id\":\"0123456789abcdef0123456789abcdef\",\"id\":\"job_1\",\"name\":\"中文.nsp\",\"wire_name\":\"job_1_中文.nsp\",\"kind\":\"upload\",\"stage\":\"transferring\",\"bytes_done\":315000000,\"bytes_total\":3005823376,\"speed\":10485760,\"progress\":10.5}";
static void replace(char* out, const char* needle, const char* replacement) {
    const char* at = strstr(good, needle);
    assert(at);
    size_t prefix = at - good;
    memcpy(out, good, prefix);
    strcpy(out + prefix, replacement);
    strcat(out, at + strlen(needle));
}
int main(void) {
    SlTaskProgress task = {0}, next = {0};
    char json[4096];
    assert(sl_parse_task_progress(good, strlen(good), device, &task));
    assert(task.bytes_total == 3005823376ULL && task.bytes_done == 315000000);
    assert(sl_task_percent(&task) > 10.47 && sl_task_percent(&task) < 10.49);
    assert(task.speed == 10485760 && !strcmp(task.name, "中文.nsp"));
    replace(json, "\"job_1_中文.nsp\"", "\"\"");
    assert(sl_parse_task_progress(json, strlen(json), device, &next));
    assert(!sl_parse_task_progress(good, strlen(good), "ffffffffffffffffffffffffffffffff", &next));
    assert(!sl_parse_task_progress("{}", 2, device, &next));
    assert(!sl_parse_task_progress("{", 1, device, &next));
    assert(!sl_parse_task_progress(good, 2049, device, &next));
    assert(!sl_parse_task_progress(NULL, 0, device, &next));
    const char* needles[] = {"\"version\":1", "\"upload\"", "\"transferring\"", "315000000", "3005823376", "10485760", "10.5", "\"job_1\"", "\"中文.nsp\""};
    const char* replacements[] = {"\"version\":2", "\"erase\"", "\"random\"", "-1", "1", "-1", "101", "\"a\\n\"", "\"a\\u0000b\""};
    for (size_t i = 0; i < sizeof(needles)/sizeof(needles[0]); i++) {
        replace(json, needles[i], replacements[i]);
        assert(!sl_parse_task_progress(json, strlen(json), device, &next));
    }
    replace(json, "315000000", "0");
    assert(sl_parse_task_progress(json, strlen(json), device, &next));
    assert(sl_task_percent(&next) == 0);
    replace(json, "10485760", "0");
    assert(sl_parse_task_progress(json, strlen(json), device, &next) && next.speed == 0);
    next = task;
    next.bytes_done--;
    assert(!sl_merge_task_progress(&task, &next));
    next = task;
    next.bytes_done++;
    assert(sl_merge_task_progress(&task, &next));
    next.stage = SlTask_Completed;
    assert(sl_merge_task_progress(&task, &next));
    next.stage = SlTask_Transferring;
    assert(!sl_merge_task_progress(&task, &next));
    strcpy(next.id, "job_2");
    assert(sl_merge_task_progress(&task, &next));
    task.bytes_total = 0;
    assert(sl_task_percent(&task) < 0);
    task.bytes_done = 0;
    next.bytes_total = 3005823376ULL;
    assert(sl_merge_task_progress(&task, &next));
    assert(sl_task_terminal(SlTask_Failed) && sl_task_terminal(SlTask_Cancelled));
    for (unsigned seed = 1; seed < 1000; seed++) {
        unsigned value = seed;
        for (size_t j = 0; j < 64; j++) { value = value * 1664525U + 1013904223U; json[j] = value >> 24; }
        assert(!sl_parse_task_progress(json, 64, device, &next));
    }
    puts("Task progress parser, identity, bounds, zero-speed and ordering tests passed.");
    return 0;
}
