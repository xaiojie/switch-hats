#pragma once
#include "engine.hpp"
namespace papa {
// Single audio-worker access. UI writes only desiredPaused; no native sink calls.
class PauseGateSink final:public AudioSink {
public:
 PauseGateSink(AudioSink& sink,std::atomic<bool>& desiredPaused):sink_(sink),desired_(desiredPaused){}
 bool Reset(std::string& error)override;
 bool Pause(bool paused,std::string& error)override;
 bool Poll(uint64_t& frames,std::string& error)override;
 bool Room()const override{return held_.empty()&&!desired_.load()&&sink_.Room();}
 bool Empty()const override{return held_.empty()&&sink_.Empty();}
 bool Submit(const int16_t* samples,size_t frames,std::string& error)override;
private:
 bool Flush(std::string& error);
 AudioSink&sink_;std::atomic<bool>&desired_;std::vector<int16_t>held_;
};
}
