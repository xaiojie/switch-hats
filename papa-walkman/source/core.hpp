#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace papa {
enum class SourceKind { SD, Cloud };
struct Track {
    std::string path, title, artist;
    std::string id{}, album = "未知专辑", albumArtist{}, cover{}, lyrics{}, sourceId{};
    int trackNumber = 0, discNumber = 0;
    double duration = 0;
    uint64_t fileSize = 0, modified = 0;
    SourceKind source = SourceKind::SD;
};
enum class Mode { Sequential, LoopAll, RepeatOne, Shuffle };
const char* ModeName(Mode mode);
struct Settings { int volume = 70; Mode mode = Mode::LoopAll; };
Settings LoadSettings(const std::string& path);
bool SaveSettings(const std::string& path, Settings value);
std::vector<Track> ScanLibrary(const std::string& root, size_t limit = 10000, int depth = 8);
std::string TimeLabel(double seconds);

class Queue {
public:
    explicit Queue(uint32_t seed = 1) : random_(seed) {}
    void Reset(size_t count, int current = -1);
    void Select(int index);
    void SetMode(Mode mode);
    int Next(bool automatic = false);
    int Previous();
    int Current() const { return current_; }
    Mode GetMode() const { return mode_; }
private:
    size_t count_ = 0;
    int current_ = -1;
    Mode mode_ = Mode::LoopAll;
    std::vector<int> bag_, history_;
    std::mt19937 random_;
};

class Resampler {
public:
    void Reset(int rate, int channels);
    void Push(const int16_t* samples, size_t frames);
    size_t Pull(int16_t* stereo, size_t frames, bool eof = false);
private:
    std::vector<int16_t> fifo_;
    uint64_t phase_ = 0;
    int rate_ = 48000, channels_ = 2;
};
}
