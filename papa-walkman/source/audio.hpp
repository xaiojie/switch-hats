#pragma once
#include "engine.hpp"
#include <mutex>
#include <thread>
namespace papa {
class AudioPlayer {
public:
    AudioPlayer();~AudioPlayer();
    void Play(const std::string&path);
    void Seek(double seconds);
    void Pause(bool paused);
    void Volume(int volume);
    Playback Snapshot();
private:
    void Run();
    std::mutex mutex_;std::atomic<bool>running_{true};std::atomic<uint64_t>generation_{0};
    Playback snapshot_;std::string pendingPath_,fatalError_;
    bool pendingPlay_=false,paused_=true,pendingPause_=false;
    double seek_=-1;int volume_=70;
    std::thread worker_;
};
}
