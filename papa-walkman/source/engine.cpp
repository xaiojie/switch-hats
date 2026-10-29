#include "engine.hpp"
#include <algorithm>
#include <cmath>
namespace papa {
bool Engine::Open(const std::string&path,const std::atomic<uint64_t>*cancel,uint64_t expected){
    int volume=state_.volume;state_={};state_.volume=volume;state_.path=path;opened_=false;
    sourceEof_=drained_=false;playedFrames_=0;baseSeconds_=0;gain_=0;
    std::string error;
    if(!sink_.Reset(error)){Fail(error);return false;}
    if(!decoder_.Open(path,cancel,expected)){Fail(decoder_.Error());return false;}
    opened_=true;state_.rate=decoder_.Rate();state_.duration=decoder_.Duration();
    resampler_.Reset(decoder_.Rate(),decoder_.Channels());Pause(false);
    return state_.error.empty();
}
void Engine::Seek(double seconds){
    if(!opened_||!state_.error.empty())return;
    if(!std::isfinite(seconds))seconds=0;
    seconds=std::max(0.0,std::min(seconds,state_.duration));
    std::string error;bool paused=state_.paused;
    if(!sink_.Reset(error)){Fail(error);return;}
    if(!decoder_.Seek(seconds)){Fail(decoder_.Error());return;}
    resampler_.Reset(decoder_.Rate(),decoder_.Channels());
    sourceEof_=drained_=false;playedFrames_=0;baseSeconds_=seconds;gain_=0;
    state_.seconds=seconds;state_.ended=false;Pause(paused);
}
void Engine::Pause(bool pause){
    std::string error;if(!sink_.Pause(pause,error)){Fail(error);return;}state_.paused=pause;
}
void Engine::Volume(int value){state_.volume=std::max(0,std::min(100,value));}
void Engine::Fail(const std::string&error){
    state_.error=error.empty()?"音频输出失败，请重新打开应用":error;state_.paused=true;opened_=false;
    std::string ignored;sink_.Reset(ignored);
}
void Engine::Tick(){
    if(!opened_||!state_.error.empty()||state_.ended)return;
    uint64_t played=0;std::string error;
    if(!sink_.Poll(played,error)){Fail(error);return;}
    playedFrames_+=played;state_.seconds=std::min(state_.duration,baseSeconds_+playedFrames_/48000.0);
    if(state_.paused)return;
    while(sink_.Room()&&!drained_){
        int16_t output[1024*2],input[2048*2];size_t made=0;
        while(made<1024){
            size_t got=resampler_.Pull(output+made*2,1024-made,sourceEof_);made+=got;
            if(made==1024)break;
            if(sourceEof_){drained_=true;break;}
            size_t read=decoder_.Read(input,2048);
            if(!decoder_.Error().empty()){Fail(decoder_.Error());return;}
            if(!read)sourceEof_=true;else resampler_.Push(input,read);
        }
        if(made){
            double target=state_.volume/100.0;target*=target;
            for(size_t n=0;n<made;++n){gain_+=std::max(-1.0/960,std::min(1.0/960,target-gain_));for(int c=0;c<2;++c)output[n*2+c]=static_cast<int16_t>(output[n*2+c]*gain_);}
            if(!sink_.Submit(output,made,error)){Fail(error);return;}
        }
    }
    if(drained_&&sink_.Empty()){state_.ended=true;state_.seconds=state_.duration;state_.completion=++completionCounter_;}
}
}
