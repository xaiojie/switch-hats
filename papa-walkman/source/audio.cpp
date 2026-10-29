#include "audio.hpp"
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace papa {
namespace {
class SwitchSink final:public AudioSink {
public:
    bool Init(std::string&error){
        Result rc=audoutInitialize();if(R_FAILED(rc)){char text[128];std::snprintf(text,sizeof(text),"音频初始化失败 (%08x)，请按住 R 启动游戏进入自制菜单",rc);error=text;return false;}
        active_=true;
        if(audoutGetSampleRate()!=48000||audoutGetChannelCount()!=2||audoutGetPcmFormat()!=PcmFormat_Int16){error="当前音频输出格式不兼容";return false;}
        for(int i=0;i<3;++i){buffers_[i].buffer=pcm_[i];buffers_[i].buffer_size=sizeof(pcm_[i]);}
        return true;
    }
    ~SwitchSink(){if(active_){std::string ignored;Reset(ignored);audoutExit();}}
    bool Reset(std::string&error)override{
        if(!active_)return true;
        if(started_&&!Check(audoutStopAudioOut(),"停止音频",error))return false;
        started_=false;bool flushed=false;
        if(!Check(audoutFlushAudioOutBuffers(&flushed),"清理音频缓冲",error))return false;
        uint64_t ignored=0;if(!Poll(ignored,error))return false;
        for(int i=0;i<3;++i){bool contains=false;if(!Check(audoutContainsAudioOutBuffer(&buffers_[i],&contains),"检查音频缓冲",error))return false;
            if(contains){error="旧音频缓冲尚未归还，请退出后重新打开";return false;}used_[i]=false;}
        paused_=false;return true;
    }
    bool Pause(bool pause,std::string&error)override{
        if(pause&&started_){if(!Check(audoutStopAudioOut(),"暂停音频",error))return false;started_=false;}
        paused_=pause;
        if(!pause&&!started_&&!Empty()){if(!Check(audoutStartAudioOut(),"继续音频",error))return false;started_=true;}
        return true;
    }
    bool Poll(uint64_t&frames,std::string&error)override{
        frames=0;
        for(int n=0;n<3;++n){AudioOutBuffer*released=nullptr;u32 count=0;
            if(!Check(audoutGetReleasedAudioOutBuffer(&released,&count),"回收音频缓冲",error))return false;
            if(!count)break;
            bool found=false;for(int i=0;i<3;++i)if(released==&buffers_[i]){if(used_[i])frames+=buffers_[i].data_size/4;used_[i]=false;found=true;break;}
            if(!found){error="音频服务返回了未知缓冲";return false;}
        }
        return true;
    }
    bool Room()const override{return !used_[0]||!used_[1]||!used_[2];}
    bool Empty()const override{return !used_[0]&&!used_[1]&&!used_[2];}
    bool Submit(const int16_t*stereo,size_t frames,std::string&error)override{
        if(frames==0||frames>1024){error="音频块大小错误";return false;}
        int slot=-1;for(int i=0;i<3;++i)if(!used_[i]){slot=i;break;}
        if(slot<0){error="音频缓冲已满";return false;}
        std::memcpy(pcm_[slot],stereo,frames*4);auto&b=buffers_[slot];b.data_size=frames*4;b.data_offset=0;b.next=nullptr;
        if(!Check(audoutAppendAudioOutBuffer(&b),"提交音频",error))return false;
        used_[slot]=true;
        if(!paused_&&!started_){if(!Check(audoutStartAudioOut(),"启动音频",error))return false;started_=true;}
        return true;
    }
private:
    static bool Check(Result rc,const char*what,std::string&error){if(R_SUCCEEDED(rc))return true;char text[160];std::snprintf(text,sizeof(text),"%s失败 (%08x)",what,rc);error=text;return false;}
    alignas(0x1000) int16_t pcm_[3][1024*2]{};
    AudioOutBuffer buffers_[3]{};bool used_[3]{};bool active_=false,started_=false,paused_=false;
};
}
AudioPlayer::AudioPlayer():worker_([this]{Run();}){}
AudioPlayer::~AudioPlayer(){running_=false;generation_.fetch_add(1);if(worker_.joinable())worker_.join();}
void AudioPlayer::Play(const std::string&path){std::lock_guard<std::mutex>lock(mutex_);if(!fatalError_.empty()){snapshot_.error=fatalError_;snapshot_.loading=false;return;}pendingPath_=path;pendingPlay_=true;paused_=false;pendingPause_=false;seek_=-1;generation_.fetch_add(1);snapshot_.path=path;snapshot_.loading=true;snapshot_.ended=false;snapshot_.error.clear();snapshot_.seconds=0;snapshot_.duration=0;snapshot_.revision=generation_.load();}
void AudioPlayer::Seek(double seconds){std::lock_guard<std::mutex>lock(mutex_);seek_=seconds;}
void AudioPlayer::Pause(bool paused){std::lock_guard<std::mutex>lock(mutex_);paused_=paused;pendingPause_=true;snapshot_.paused=paused;}
void AudioPlayer::Volume(int volume){std::lock_guard<std::mutex>lock(mutex_);volume_=std::max(0,std::min(100,volume));}
Playback AudioPlayer::Snapshot(){std::lock_guard<std::mutex>lock(mutex_);return snapshot_;}
void AudioPlayer::Run(){
    SwitchSink sink;std::string error;
    if(!sink.Init(error)){std::lock_guard<std::mutex>lock(mutex_);fatalError_=error;snapshot_.error=error;snapshot_.loading=false;return;}
    Engine engine(sink);uint64_t activeRevision=0;
    while(running_){
        std::string path;bool play=false,pause=false,doPause=false;double seek=-1;int volume=70;uint64_t revision=0;
        {std::lock_guard<std::mutex>lock(mutex_);play=pendingPlay_;if(play){path=pendingPath_;pendingPlay_=false;}doPause=pendingPause_;pause=paused_;pendingPause_=false;seek=seek_;seek_=-1;volume=volume_;revision=generation_.load();}
        engine.Volume(volume);
        if(play){activeRevision=revision;engine.Open(path,&generation_,revision);}
        if(doPause)engine.Pause(pause);
        if(seek>=0)engine.Seek(seek);
        engine.Tick();
        {std::lock_guard<std::mutex>lock(mutex_);if(generation_.load()==activeRevision&&!pendingPlay_&&!pendingPause_&&seek_<0){snapshot_=engine.State();snapshot_.revision=activeRevision;}}
        svcSleepThread(4'000'000);
    }
}
}
