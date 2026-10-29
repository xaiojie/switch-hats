#include "switch_link.hpp"
#include "ftpsrv_helper.hpp"
#include <curl/curl.h>
#include "log.hpp"
#include <yyjson.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <algorithm>
#include <cctype>

namespace sphaira::switch_link {
namespace {
constexpr unsigned DISCOVERY_PORT = 28771;
constexpr unsigned PROGRESS_PORT = 28772;
constexpr const char* CONFIG_DIR = "sdmc:/config/switch-link";
constexpr const char* RECEIPT = "sdmc:/config/switch-link/last-install.json";
constexpr const char* LAST_JOB = "sdmc:/config/switch-link/last-http-job";
constexpr const char* LAST_JOB_TMP = "sdmc:/config/switch-link/last-http-job.tmp";
constexpr const char* RECEIPT_TMP = "sdmc:/config/switch-link/last-install.json.tmp";
int beacon_socket = -1;
int progress_socket = -1;
u64 last_beacon = 0;
u64 last_socket_attempt = 0;
char device_id[33]{};
char last_http_job[33]{};
bool last_job_loaded{};
bool receiver_ready{}, http_pending{}, http_busy{};
SlHttpInstall pending_command{};
thread_local unsigned registered_titles = 0;
Mutex task_mutex{};
SlTaskProgress task{};
bool native_active{};
bool native_terminal{};
bool registering{};
u64 last_pc_update{};
u64 last_task_update{};
u64 native_done{}, native_total{}, native_speed{};
char native_step[257]{};
struct TaskLock {
    TaskLock() { mutexLock(&task_mutex); }
    ~TaskLock() { mutexUnlock(&task_mutex); }
};
u64 Now() { return armTicksToNs(armGetSystemTick()); }

void LoadLastJob() {
    if (last_job_loaded) return;
    last_job_loaded = true;
    // A fully flushed temporary ID also blocks replay after interrupted rename.
    for (const auto path : {LAST_JOB_TMP, LAST_JOB}) {
        if (auto file = fopen(path, "rb")) {
            char value[34]{};
            const auto count = fread(value, 1, sizeof(value), file);
            fclose(file);
            bool valid = count == 32;
            for (unsigned i = 0; valid && i < 32; i++) valid = (value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f');
            if (valid) { memcpy(last_http_job, value, 32); return; }
        }
    }
}
void LoadDeviceId() {
    if (device_id[0]) return;
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    if (auto file = fopen("sdmc:/config/switch-link/device-id", "rb")) {
        const auto count = fread(device_id, 1, 32, file);
        fclose(file);
        bool valid = count == 32;
        for (size_t i = 0; i < count; i++)
            valid &= (device_id[i] >= '0' && device_id[i] <= '9') || (device_id[i] >= 'a' && device_id[i] <= 'f');
        if (valid) return;
    }
    u8 bytes[16];
    randomGet(bytes, sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); i++) snprintf(device_id + i * 2, 3, "%02x", bytes[i]);
    if (auto file = fopen("sdmc:/config/switch-link/device-id", "wb")) {
        fwrite(device_id, 1, 32, file);
        fclose(file);
    }
}
}

TaskSnapshot GetTaskSnapshot() {
    TaskLock lock;
    TaskSnapshot out;
    out.task = task;
    out.visible = task.id[0] && (native_active || !sl_task_terminal(task.stage) || Now() - last_task_update < 30'000'000'000ULL);
    out.native_active = native_active;
    out.native_terminal = native_terminal;
    out.stale = !native_active && !sl_task_terminal(task.stage) && Now() - last_pc_update > 10'000'000'000ULL;
    if (native_active) {
        out.task.stage = registering ? SlTask_Registering : SlTask_Installing;
        out.task.bytes_done = native_done;
        out.task.bytes_total = native_total;
        out.task.speed = native_speed;
        snprintf(out.step, sizeof(out.step), "%s", native_step);
    }
    return out;
}

void UiTick(unsigned port) {
    LoadDeviceId();
    LoadLastJob();
    const auto now = Now();
    if (progress_socket < 0 && (!last_socket_attempt || now - last_socket_attempt > 2'000'000'000ULL)) {
        last_socket_attempt = now;
        progress_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (progress_socket >= 0) {
            int enable = 1;
            setsockopt(progress_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_port = htons(PROGRESS_PORT);
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            const int flags = fcntl(progress_socket, F_GETFL, 0);
            if (flags < 0 || fcntl(progress_socket, F_SETFL, flags | O_NONBLOCK) < 0
                || bind(progress_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
                close(progress_socket);
                progress_socket = -1;
                log_write("[Switch Link] Could not open task progress UDP port\n");
            }
        }
    }
    // This socket is owned exclusively by the UI thread. Never wait for a packet,
    // and cap parsing work per frame even if the network queues a flood.
    for (unsigned i = 0; progress_socket >= 0 && i < 8; i++) {
        char packet[4097];
        sockaddr_in sender{};
        socklen_t sender_size = sizeof(sender);
        const auto size = recvfrom(progress_socket, packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&sender), &sender_size);
        if (size <= 0) break;
        if (size > 4096 || sender.sin_family != AF_INET) continue;
        char peer[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sender.sin_addr, peer, sizeof(peer));
        SlHttpInstall command{};
        if (sl_parse_http_install(packet, size, device_id, peer, &command)) {
            TaskLock lock;
            if (sl_http_install_can_accept(command.id, last_http_job, http_busy || native_active, receiver_ready)) {
                pending_command = command;
                http_pending = http_busy = true;
                memcpy(last_http_job, command.id, sizeof(last_http_job));
            }
            continue;
        }
        SlTaskProgress incoming{};
        if (!sl_parse_task_progress(packet, size, device_id, &incoming)) continue;
        TaskLock lock;
        const bool same = !strcmp(task.id, incoming.id);
        if ((native_active || http_busy) && !same) continue;
        if (same && native_terminal) continue;
        // A PC transfer finishing is not evidence that an application was registered.
        if (incoming.kind == SlTask_Install && incoming.stage == SlTask_Completed) incoming.stage = SlTask_Installing;
        if (!sl_merge_task_progress(&task, &incoming)) continue;
        if (!same) native_terminal = false;
        last_pc_update = last_task_update = now;
    }
    BeaconTick(port);
}

void BeaconTick(unsigned port) {
    const auto now = Now();
    const auto snapshot = GetTaskSnapshot();
    const u64 interval = snapshot.visible ? 500'000'000ULL : 2'000'000'000ULL;
    if (now - last_beacon < interval) return;
    last_beacon = now;
    LoadDeviceId();
    if (beacon_socket < 0) {
        beacon_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (beacon_socket < 0) return;
        int enable = 1;
        const int flags = fcntl(beacon_socket, F_GETFL, 0);
        if (flags < 0 || fcntl(beacon_socket, F_SETFL, flags | O_NONBLOCK) < 0
            || setsockopt(beacon_socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0) {
            BeaconClose();
            return;
        }
    }
    auto doc = yyjson_mut_doc_new(nullptr);
    if (!doc) return;
    auto root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "type", "switch-link");
    yyjson_mut_obj_add_uint(doc, root, "version", 1);
    yyjson_mut_obj_add_str(doc, root, "name", "Switch Link 0.2.3");
    yyjson_mut_obj_add_str(doc, root, "app_version", "0.2.3");
    yyjson_mut_obj_add_uint(doc, root, "ftp_port", port);
    yyjson_mut_obj_add_uint(doc, root, "progress_port", PROGRESS_PORT);
    yyjson_mut_obj_add_bool(doc, root, "progress_ready", progress_socket >= 0);
    yyjson_mut_obj_add_str(doc, root, "device_id", device_id);
    yyjson_mut_obj_add_str(doc, root, "sd_root", "/sdmc:");
    yyjson_mut_obj_add_str(doc, root, "install_path", "/install:");
    yyjson_mut_obj_add_str(doc, root, "receipt_path", "/sdmc:/config/switch-link/last-install.json");
    yyjson_mut_obj_add_bool(doc, root, "receipt_supported", true);
    yyjson_mut_obj_add_bool(doc, root, "install_ready", false);
    yyjson_mut_obj_add_bool(doc, root, "http_install_supported", true);
    yyjson_mut_obj_add_uint(doc, root, "command_port", PROGRESS_PORT);
    auto formats = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_str(doc, formats, "nsp");
    yyjson_mut_arr_add_str(doc, formats, "xci");
    yyjson_mut_obj_add_val(doc, root, "http_formats", formats);
    {
        TaskLock lock;
        yyjson_mut_obj_add_bool(doc, root, "http_install_ready", receiver_ready && !http_busy && !native_active && progress_socket >= 0);
    }
    if (snapshot.visible) {
        const auto& t = snapshot.task;
        auto detail = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_val(doc, root, "task", detail);
        yyjson_mut_obj_add_str(doc, detail, "id", t.id);
        yyjson_mut_obj_add_str(doc, detail, "name", t.name);
        yyjson_mut_obj_add_str(doc, detail, "kind", t.kind == SlTask_Install ? "install" : "upload");
        yyjson_mut_obj_add_str(doc, detail, "stage", sl_task_stage_name(t.stage));
        yyjson_mut_obj_add_str(doc, detail, "source", snapshot.native_active || snapshot.native_terminal ? "console" : "pc");
        yyjson_mut_obj_add_str(doc, detail, "step", snapshot.step);
        yyjson_mut_obj_add_uint(doc, detail, "bytes_done", t.bytes_done);
        yyjson_mut_obj_add_uint(doc, detail, "bytes_total", t.bytes_total);
        yyjson_mut_obj_add_real(doc, detail, "speed", t.speed);
        double percent = t.stage == SlTask_Completed ? 100 : sl_task_percent(&t);
        if (t.kind == SlTask_Install && !snapshot.native_active && !snapshot.native_terminal && t.stage != SlTask_Transferring) percent = -1;
        yyjson_mut_obj_add_bool(doc, detail, "determinate", percent >= 0);
        if (percent >= 0) yyjson_mut_obj_add_real(doc, detail, "progress", percent);
        if (t.bytes_total > t.bytes_done && t.speed > 0)
            yyjson_mut_obj_add_uint(doc, detail, "eta_seconds", static_cast<u64>((t.bytes_total - t.bytes_done) / t.speed) + 1);
    }
    size_t size{};
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &size);
    yyjson_mut_doc_free(doc);
    if (!json) return;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(DISCOVERY_PORT);
    u32 addr{}, mask{}, gateway{}, primary{}, secondary{};
    if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&addr, &mask, &gateway, &primary, &secondary)) && addr && mask) {
        target.sin_addr.s_addr = addr | ~mask;
        sendto(beacon_socket, json, size, 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    }
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(beacon_socket, json, size, 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    free(json);
}

void BeaconClose() {
    if (beacon_socket >= 0) close(beacon_socket);
    beacon_socket = -1;
    if (progress_socket >= 0) close(progress_socket);
    progress_socket = -1;
}

void SetHttpReceiverReady(bool ready) {
    TaskLock lock;
    receiver_ready = ready;
}
bool TakeHttpInstall(SlHttpInstall& command) {
    TaskLock lock;
    if (!http_pending || !receiver_ready) return false;
    command = pending_command;
    pending_command = {};
    http_pending = receiver_ready = false;
    return true;
}
bool PersistHttpInstall(const SlHttpInstall& command) {
    // Worker persists the reserved ID before starting installation or HTTP.
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    bool ok = false;
    if (auto file = fopen(LAST_JOB_TMP, "wb")) {
        ok = fwrite(command.id, 1, 32, file) == 32;
        ok = fflush(file) == 0 && ok;
        ok = fsync(fileno(file)) == 0 && ok;
        ok = fclose(file) == 0 && ok;
        if (ok) {
            remove(LAST_JOB);
            ok = rename(LAST_JOB_TMP, LAST_JOB) == 0;
        }
    }
    return ok;
}
bool HttpInstallSucceeded(Result result) {
    return sl_http_install_success(R_VALUE(result), registered_titles);
}
void PostHttpInstallResult(const SlHttpInstall& command, Result result) {
    auto doc = yyjson_mut_doc_new(nullptr);
    if (!doc) return;
    auto root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    const bool success = HttpInstallSucceeded(result);
    yyjson_mut_obj_add_str(doc, root, "id", command.id);
    yyjson_mut_obj_add_str(doc, root, "device_id", device_id);
    yyjson_mut_obj_add_str(doc, root, "filename", command.wire_name);
    yyjson_mut_obj_add_bool(doc, root, "success", success);
    yyjson_mut_obj_add_uint(doc, root, "result", R_VALUE(result));
    yyjson_mut_obj_add_uint(doc, root, "registered_titles", registered_titles);
    yyjson_mut_obj_add_str(doc, root, "message", success ? "Application record registered on console" : R_FAILED(result) ? "Console installer returned an error" : "No application record was registered");
    size_t length{};
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &length);
    yyjson_mut_doc_free(doc);
    if (!json) return;
    if (auto curl = curl_easy_init()) {
        auto headers = curl_slist_append(nullptr, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, command.result_url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(length));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_PROXY, "");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP));
        // Never log capability URLs or retain the response body.
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t size, size_t count, void*) -> size_t { return size * count; });
        for (unsigned attempt = 0; attempt < 3; attempt++) {
            const auto rc = curl_easy_perform(curl);
            long code{};
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            if (rc == CURLE_OK && code >= 200 && code < 300) break;
            log_write("[Switch Link] Result delivery attempt %u failed (%d / %ld)\n", attempt + 1, rc, code);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    free(json);
}
void FinishHttpInstall() {
    TaskLock lock;
    http_busy = false;
}

void BeginInstallReceipt(const char* path) {
    registered_titles = 0;
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    TaskLock lock;
    if (strcmp(task.wire_name, filename)) {
        task = {};
        const auto length = strlen(filename);
        bool prefixed = length > 33 && filename[32] == '_';
        for (unsigned i = 0; prefixed && i < 32; i++) prefixed &= std::isxdigit(static_cast<unsigned char>(filename[i])) != 0;
        if (prefixed) memcpy(task.id, filename, 32);
        snprintf(task.name, sizeof(task.name), "%s", prefixed ? filename + 33 : filename);
        snprintf(task.wire_name, sizeof(task.wire_name), "%s", filename);
    }
    task.kind = SlTask_Install;
    task.stage = SlTask_Installing;
    native_active = true;
    native_terminal = registering = false;
    native_done = native_total = native_speed = 0;
    snprintf(native_step, sizeof(native_step), "正在读取安装包");
    last_task_update = Now();
}
void MarkRegistering() {
    TaskLock lock;
    registering = true;
    native_done = native_total = native_speed = 0;
    snprintf(native_step, sizeof(native_step), "正在提交应用记录，尚未完成");
}
void ReportInstallerProgress(const char* step, s64 offset, s64 size, s64 speed) {
    TaskLock lock;
    if (!native_active) return;
    native_total = registering ? 0 : std::max<s64>(size, 0);
    native_done = std::clamp<s64>(offset, 0, native_total);
    native_speed = std::max<s64>(speed, 0);
    snprintf(native_step, sizeof(native_step), "%s", step);
}
void RecordRegisteredTitle() { registered_titles++; }

bool WriteInstallReceipt(const char* path, Result result) {
    const bool success = sl_http_install_success(R_VALUE(result), registered_titles);
    {
        TaskLock lock;
        native_active = false;
        native_terminal = true;
        task.stage = success ? SlTask_Completed : task.stage == SlTask_Cancelled ? SlTask_Cancelled : SlTask_Failed;
        task.speed = 0;
        last_task_update = Now();
    }
    const char* filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    auto doc = yyjson_mut_doc_new(nullptr);
    if (!doc) return false;
    auto root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "filename", filename);
    yyjson_mut_obj_add_bool(doc, root, "success", success);
    yyjson_mut_obj_add_uint(doc, root, "registered_titles", registered_titles);
    yyjson_mut_obj_add_uint(doc, root, "result", R_VALUE(result));
    yyjson_mut_obj_add_str(doc, root, "message", success ? "Application record registered on console" : R_FAILED(result) ? "Console installer returned an error" : "No application record was registered");
    yyjson_mut_obj_add_sint(doc, root, "finished_at", std::time(nullptr));
    size_t size = 0;
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &size);
    yyjson_mut_doc_free(doc);
    if (!json) return false;
    mkdir("sdmc:/config", 0777);
    mkdir(CONFIG_DIR, 0777);
    bool ok = false;
    if (auto file = fopen(RECEIPT_TMP, "wb")) {
        ok = fwrite(json, 1, size, file) == size;
        ok = fflush(file) == 0 && ok;
        ok = fsync(fileno(file)) == 0 && ok;
        ok = fclose(file) == 0 && ok;
        if (ok) {
            // Horizon's rename does not replace an existing destination.
            // Readers may briefly see no receipt, but never a partial JSON file.
            remove(RECEIPT);
            ok = rename(RECEIPT_TMP, RECEIPT) == 0;
        }
    }
    free(json);
    if (!ok) {
        remove(RECEIPT_TMP);
        log_write("[Switch Link] Failed to write installation receipt\n");
    }
    return ok;
}
}
