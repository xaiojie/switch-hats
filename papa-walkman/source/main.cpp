// 爬爬的随身听 —— M1：先让它出声（libnx 原生音频版）
//
// 不依赖 SDL / mpg123：解码用内置 minimp3，输出去 audout。
// 这样构建只需要 devkitA64 + libnx，不需要联网装 portlibs。
//
// 目标：
//   1. 找到 SD 卡 /music 下的 MP3
//   2. minimp3 解码 → 重采样到系统采样率 → audout 播放
//   3. A 暂停，←/→ 切歌，↑/↓ 音量，+ 退出

#include <switch.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

namespace {

constexpr int  kBufCount     = 4;
constexpr int  kFramesPerBuf = 2048;   // 每块帧数
constexpr char kMusicRoot[]  = "sdmc:/music";

struct Track {
    std::string path;
    std::string name;
};

bool IsMp3(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(tolower(c));
    return ext == "mp3";
}

void ScanDir(const std::string& dir, std::vector<Track>& out, int depth = 0) {
    if (depth > 4) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;

    std::vector<std::string> subdirs;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name.empty() || name[0] == '.') continue;

        std::string full = dir + "/" + name;
        struct stat st {};
        if (stat(full.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            subdirs.push_back(full);
        } else if (IsMp3(name)) {
            out.push_back({full, name});
        }
    }
    closedir(d);

    std::sort(subdirs.begin(), subdirs.end());
    for (const auto& s : subdirs) ScanDir(s, out, depth + 1);
}

// 线性插值重采样：把 src 采样率拉到系统采样率
class Resampler {
public:
    void Reset(int srcRate, int dstRate, int channels) {
        m_ratio = (srcRate > 0 && dstRate > 0)
                    ? static_cast<double>(srcRate) / static_cast<double>(dstRate)
                    : 1.0;
        m_channels = channels;
        m_fifo.clear();
        m_pos = 0.0;
    }

    // 输入：交错 int16；输出：交错 int16，写到 out
    void Push(const int16_t* src, size_t frames, std::vector<int16_t>& out) {
        size_t ch = static_cast<size_t>(m_channels);
        m_fifo.insert(m_fifo.end(), src, src + frames * ch);

        size_t availableFrames = m_fifo.size() / ch;
        while (m_pos + 1.0 < static_cast<double>(availableFrames)) {
            size_t idx  = static_cast<size_t>(m_pos);
            double frac = m_pos - static_cast<double>(idx);

            for (size_t c = 0; c < ch; ++c) {
                double a = m_fifo[idx * ch + c];
                double b = m_fifo[(idx + 1) * ch + c];
                out.push_back(static_cast<int16_t>(a + (b - a) * frac));
            }
            m_pos += m_ratio;
        }

        // 丢掉已消费的整数部分
        size_t consumed = static_cast<size_t>(m_pos);
        if (consumed > 0) {
            m_fifo.erase(m_fifo.begin(),
                         m_fifo.begin() + static_cast<long>(consumed * ch));
            m_pos -= static_cast<double>(consumed);
        }
    }

    void Flush(std::vector<int16_t>& out) {
        out.insert(out.end(), m_fifo.begin(), m_fifo.end());
        m_fifo.clear();
        m_pos = 0.0;
    }

private:
    std::vector<int16_t> m_fifo;
    double m_ratio = 1.0;
    double m_pos = 0.0;
    int    m_channels = 2;
};

class Player {
public:
    bool Init() {
        if (R_FAILED(audoutInitialize())) {
            m_error = "audoutInitialize 失败（可能是 applet 模式限制）";
            return false;
        }
        u32       rate     = audoutGetSampleRate();
        u32       channels = audoutGetChannelCount();
        PcmFormat format   = audoutGetPcmFormat();

        if (format != PcmFormat_Int16) {
            m_error = "系统音频不是 16bit PCM";
            return false;
        }

        m_sysRate     = static_cast<int>(rate);
        m_sysChannels = static_cast<int>(channels);

        for (int i = 0; i < kBufCount; ++i) {
            m_bufs[i].buffer      = m_pcm[i];
            m_bufs[i].buffer_size = sizeof(m_pcm[i]);
            m_bufs[i].data_size   = 0;
            m_bufs[i].data_offset = 0;
            m_bufs[i].next        = nullptr;
        }
        return true;
    }

    void Start(const std::string& firstPath) {
        m_pendingPath = firstPath;
        m_hasPending  = true;
        m_running     = true;
        m_thread = std::thread([this] { Loop(); });
    }

    void Shutdown() {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
        CloseDecoder();
        audoutExit();
    }

    void Play(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_pendingPath = path;
        m_hasPending  = true;
    }

    void TogglePause() { m_paused = !m_paused; }
    bool Paused() const { return m_paused; }
    bool Finished() const { return m_finished; }
    void ClearFinished() { m_finished = false; }

    void VolumeDelta(int d) {
        int v = m_volume + d;
        if (v < 0) v = 0;
        if (v > 128) v = 128;
        m_volume = v;
    }
    int Volume() const { return m_volume; }

    int SourceRate() const { return m_srcRate; }
    int SourceChannels() const { return m_srcChannels; }
    int SysRate() const { return m_sysRate; }
    const std::string& Error() const { return m_error; }

    double Seconds() const {
        if (!m_haveDecoder) return 0.0;
        if (m_srcRate <= 0) return 0.0;
        return static_cast<double>(m_decPlayed / static_cast<u64>(m_srcChannels)) /
               m_srcRate;
    }
    double Duration() const {
        if (!m_haveDecoder || m_srcRate <= 0) return 0.0;
        return static_cast<double>(m_decTotal / static_cast<u64>(m_srcChannels)) /
               m_srcRate;
    }

private:
    void Loop() {
        FillAll();

        while (m_running) {
            if (m_hasPending) {
                std::string path;
                {
                    std::lock_guard<std::mutex> lock(m_lock);
                    path = m_pendingPath;
                    m_hasPending = false;
                }
                OpenDecoder(path);
                FillAll();
                continue;
            }

            if (m_paused || !m_haveDecoder) {
                svcSleepThread(10'000'000ULL);
                continue;
            }

            AudioOutBuffer* released[kBufCount] = {};
            u32 count = 0;
            if (R_FAILED(audoutWaitPlayFinish(released, &count, 200'000'000ULL))) {
                continue;
            }
            for (u32 i = 0; i < count; ++i) {
                FillOne(released[i]);
                audoutAppendAudioOutBuffer(released[i]);
            }
        }
    }

    void FillAll() {
        for (int i = 0; i < kBufCount; ++i) {
            if (m_bufs[i].data_size == 0 && m_haveDecoder) {
                FillOne(&m_bufs[i]);
                audoutAppendAudioOutBuffer(&m_bufs[i]);
            }
        }
    }

    void FillOne(AudioOutBuffer* buf) {
        auto* dst = static_cast<int16_t*>(const_cast<void*>(buf->buffer));
        buf->data_size = 0;
        buf->data_offset = 0;

        if (!m_haveDecoder || !m_playing) return;

        std::vector<int16_t> mixed;
        mixed.reserve(static_cast<size_t>(kFramesPerBuf) * 2);

        int16_t frame[kFramesPerBuf * 2];
        while (mixed.size() / 2 < static_cast<size_t>(kFramesPerBuf)) {
            int got = mp3dec_ex_read(&m_dec, frame, kFramesPerBuf);
            if (got <= 0) {
                m_playing = false;
                m_finished = true;
                break;
            }
            m_decPlayed += static_cast<u64>(got);
            size_t gotFrames = static_cast<size_t>(got) / static_cast<size_t>(m_srcChannels);
            ConvertChannels(frame, gotFrames, mixed);
        }

        // 重采样到系统采样率
        std::vector<int16_t> resampled;
        resampled.reserve(mixed.size());
        m_resampler.Push(mixed.data(), mixed.size() / 2, resampled);
        if (m_playing == false) m_resampler.Flush(resampled);

        // 声道对齐到系统声道数
        std::vector<int16_t> out;
        AlignChannels(resampled, out);

        ApplyGain(out.data(), out.size());

        size_t bytes = std::min(out.size() * sizeof(int16_t),
                                static_cast<size_t>(sizeof(m_pcm[0])));
        memcpy(dst, out.data(), bytes);
        buf->data_size = bytes;
    }

    void ConvertChannels(const int16_t* src, size_t frames,
                         std::vector<int16_t>& out) {
        for (size_t i = 0; i < frames; ++i) {
            if (m_srcChannels == 1) {
                out.push_back(src[i]);
                out.push_back(src[i]);
            } else {
                out.push_back(src[i * 2 + 0]);
                out.push_back(src[i * 2 + 1]);
            }
        }
    }

    void AlignChannels(const std::vector<int16_t>& in, std::vector<int16_t>& out) {
        if (m_sysChannels == 2) {
            out = in;
            return;
        }
        // 系统单声道：左右取平均
        for (size_t i = 0; i + 1 < in.size(); i += 2) {
            out.push_back(static_cast<int16_t>((static_cast<int>(in[i]) + in[i + 1]) / 2));
        }
    }

    void ApplyGain(int16_t* data, size_t count) {
        int vol = m_volume;
        if (vol == 128) return;
        for (size_t i = 0; i < count; ++i) {
            data[i] = static_cast<int16_t>((static_cast<int>(data[i]) * vol) / 128);
        }
    }

    void OpenDecoder(const std::string& path) {
        CloseDecoder();
        std::memset(&m_dec, 0, sizeof(m_dec));

        if (mp3dec_ex_open(&m_dec, path.c_str(), MP3D_SEEK_TO_SAMPLE) != 0) {
            m_error = "打开失败：" + path;
            return;
        }

        m_haveDecoder = m_playing = true;
        m_finished    = false;
        m_decPlayed   = 0;
        m_decTotal    = static_cast<u64>(m_dec.samples);
        m_srcRate     = static_cast<int>(m_dec.info.hz);
        m_srcChannels = static_cast<int>(m_dec.info.channels);

        m_resampler.Reset(m_srcRate, m_sysRate, 2);
        printf("[audio] %s  %d Hz / %d ch\n", path.c_str(), m_srcRate, m_srcChannels);
    }

    void CloseDecoder() {
        if (m_haveDecoder) {
            mp3dec_ex_close(&m_dec);
            m_haveDecoder = false;
            m_playing = false;
        }
    }

    // 音频输出
    u32 m_sysRate = 48000;
    int m_sysChannels = 2;
    AudioOutBuffer m_bufs[kBufCount] {};
    alignas(0x1000) int16_t m_pcm[kBufCount][kFramesPerBuf * 2] {};

    // 解码状态
    mp3dec_ex_t m_dec {};
    bool m_haveDecoder = false;
    bool m_playing = false;
    std::atomic<bool> m_finished {false};
    int m_srcRate = 44100;
    int m_srcChannels = 2;
    u64 m_decPlayed = 0;
    u64 m_decTotal = 0;
    Resampler m_resampler;

    // 控制
    std::mutex m_lock;
    std::string m_pendingPath;
    std::atomic<bool> m_hasPending {false};
    std::atomic<bool> m_paused {false};
    std::atomic<bool> m_running {false};
    std::atomic<int>  m_volume {100};
    std::string m_error;
    std::thread m_thread;
};

void WaitPlus() {
    PadState pad;
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
}

}  // namespace

int main(int argc, char** argv) {
    consoleInit(nullptr);
    appletSetAutoSleepDisabled(true);

    printf("\x1b[2J");
    printf("\n  爬爬的随身听 · M1\n");
    printf("  -----------------------\n\n");

    std::vector<Track> tracks;
    ScanDir(kMusicRoot, tracks);
    printf("  在 %s 找到 %zu 首 MP3\n\n", kMusicRoot, tracks.size());

    if (tracks.empty()) {
        printf("  没有歌。把 MP3 放进 sdmc:/music 再启动。\n");
        printf("  按 + 退出。\n");
        WaitPlus();
        consoleExit(nullptr);
        appletSetAutoSleepDisabled(false);
        return 0;
    }

    Player player;
    if (!player.Init()) {
        printf("  音频初始化失败：%s\n", player.Error().c_str());
        printf("  按 + 退出。\n");
        WaitPlus();
        consoleExit(nullptr);
        appletSetAutoSleepDisabled(false);
        return 0;
    }

    printf("  系统输出 %d Hz / %d ch\n", player.SysRate(), 2);

    size_t index = 0;
    player.Start(tracks[index].path);

    PadState pad;
    padInitializeDefault(&pad);

    u64 lastDraw = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus) break;

        if (down & HidNpadButton_A) {
            player.TogglePause();
        }
        if (down & HidNpadButton_Left) {
            index = (index + tracks.size() - 1) % tracks.size();
            player.Play(tracks[index].path);
        }
        if (down & HidNpadButton_Right) {
            index = (index + 1) % tracks.size();
            player.Play(tracks[index].path);
        }
        if (down & HidNpadButton_Up)   player.VolumeDelta(+5);
        if (down & HidNpadButton_Down) player.VolumeDelta(-5);

        if (player.Finished()) {
            player.ClearFinished();
            index = (index + 1) % tracks.size();
            player.Play(tracks[index].path);
        }

        u64 now = svcGetSystemTick();
        if (now - lastDraw > 60'000'000ULL) {   // 每 ~0.5 秒刷一次屏
            lastDraw = now;
            auto sec  = static_cast<int>(player.Seconds());
            auto dur  = static_cast<int>(player.Duration());
            printf("\x1b[H");
            printf("\n  爬爬的随身听 · M1\n");
            printf("  -----------------------\n\n");
            printf("  %s\n", tracks[index].name.c_str());
            printf("  %02d:%02d / %02d:%02d   %s   音量 %d%%\n\n",
                   sec / 60, sec % 60, dur / 60, dur % 60,
                   player.Paused() ? "⏸" : "▶", player.Volume() * 100 / 128);
            printf("  %d Hz -> %d Hz   %d/%zu\n",
                   player.SourceRate(), player.SysRate(), static_cast<int>(index + 1),
                   tracks.size());
            printf("\n  A 暂停   ←/→ 切歌   ↑/↓ 音量   + 退出\n");
        }

        svcSleepThread(10'000'000ULL);
    }

    player.Shutdown();
    appletSetAutoSleepDisabled(false);
    consoleExit(nullptr);
    return 0;
}
