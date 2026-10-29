#pragma once
#include "engine.hpp"
#include <mutex>
#include <thread>
namespace papa {
class AudioPlayer {
public:
    AudioPlayer();explicit AudioPlayer(AudioSink& sink);~AudioPlayer();
    void Play(const std::string&path,double startSeconds=0,bool paused=false);
    void Stop();
    void Seek(double seconds);
    void Pause(bool paused);
    void Volume(int volume);
    Playback Snapshot();
    StreamSnapshot StreamSnapshotValue();
    void SetStreamOptions(StreamOptions options);
private:
    void Run();
    std::mutex mutex_;std::atomic<bool>running_{true};std::atomic<uint64_t>generation_{0};
    Playback snapshot_;std::string pendingPath_,fatalError_;
    bool pendingPlay_=false,pendingStop_=false,pendingPause_=false,startPaused_=false;
    std::atomic<bool>paused_{true};
    double seek_=-1,startSeconds_=0;int volume_=70;
    AudioSink* injected_=nullptr;
    StreamOptions options_;
    std::shared_ptr<StreamStatus> status_;
    std::thread worker_;
};
}
