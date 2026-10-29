#pragma once
#include "core.hpp"
#include "decoder.hpp"
namespace papa {
struct Playback {
    std::string path,error;
    bool paused=true,loading=false,ended=false;
    bool buffering=false,seekable=false,durationKnown=false;
    double seconds=0,duration=0;
    int rate=0,volume=70;
    uint64_t revision=0;
    uint64_t completion=0;
};
class AudioSink {
public:
    virtual ~AudioSink()=default;
    virtual bool Reset(std::string& error)=0;
    virtual bool Pause(bool pause,std::string& error)=0;
    virtual bool Poll(uint64_t& playedFrames,std::string& error)=0;
    virtual bool Room() const=0;
    virtual bool Empty() const=0;
    virtual bool Submit(const int16_t* stereo,size_t frames,std::string& error)=0;
};
class Engine {
public:
    explicit Engine(AudioSink& sink):sink_(sink){}
    bool Open(const std::string& path,const std::atomic<uint64_t>* cancel=nullptr,uint64_t expected=0,std::shared_ptr<StreamStatus> status={},StreamOptions options={});
    void Stop();
    void Seek(double seconds);
    void Pause(bool pause);
    void Volume(int value);
    void Tick();
    const Playback& State() const{return state_;}
private:
    void Fail(const std::string& error);
    AudioSink& sink_; Decoder decoder_; Resampler resampler_; Playback state_;
    bool sourceEof_=false,drained_=false,opened_=false;
    uint64_t playedFrames_=0,completionCounter_=0;
    double baseSeconds_=0,gain_=0;
};
}
