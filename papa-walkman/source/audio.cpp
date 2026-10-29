#include "audio.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "pause_gate.hpp"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace papa {
namespace {
#ifdef __SWITCH__
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
#endif
}
AudioPlayer::AudioPlayer():worker_([this]{Run();}){}
AudioPlayer::AudioPlayer(AudioSink&sink):injected_(&sink),worker_([this]{Run();}){}
AudioPlayer::~AudioPlayer(){paused_=true;running_=false;generation_.fetch_add(1);if(worker_.joinable())worker_.join();}
void AudioPlayer::Play(const std::string&path,double start,bool paused){
 std::lock_guard<std::mutex>lock(mutex_);
 if(!fatalError_.empty()){snapshot_.error=fatalError_;snapshot_.loading=false;return;}
 pendingPath_=path;pendingPlay_=true;pendingStop_=false;startSeconds_=std::max(0.0,start);startPaused_=paused;
 paused_=paused||startSeconds_>0;pendingPause_=false;seek_=-1;generation_.fetch_add(1);
 snapshot_={};snapshot_.path=path;snapshot_.loading=true;snapshot_.paused=paused_.load();snapshot_.revision=generation_.load();
 status_=std::make_shared<StreamStatus>();
}
void AudioPlayer::Stop(){std::lock_guard<std::mutex>lock(mutex_);paused_=true;pendingStop_=true;pendingPlay_=false;generation_.fetch_add(1);snapshot_={};status_.reset();}
void AudioPlayer::Seek(double seconds){std::lock_guard<std::mutex>lock(mutex_);seek_=std::max(0.0,seconds);}
void AudioPlayer::Pause(bool paused){std::lock_guard<std::mutex>lock(mutex_);paused_=paused;pendingPause_=true;snapshot_.paused=paused;}
void AudioPlayer::Volume(int volume){std::lock_guard<std::mutex>lock(mutex_);volume_=std::max(0,std::min(100,volume));}
void AudioPlayer::SetStreamOptions(StreamOptions options){std::lock_guard<std::mutex>lock(mutex_);options_=std::move(options);}
StreamSnapshot AudioPlayer::StreamSnapshotValue(){std::shared_ptr<StreamStatus>s;{std::lock_guard<std::mutex>lock(mutex_);s=status_;}return s?s->Snapshot():StreamSnapshot{};}
Playback AudioPlayer::Snapshot(){std::lock_guard<std::mutex>lock(mutex_);auto value=snapshot_;if(status_){auto s=status_->Snapshot();value.buffering=s.buffering;if(!s.error.empty()&&value.error.empty())value.error=s.error;}return value;}
void AudioPlayer::Run(){
 std::string error;AudioSink*target=injected_;
#ifdef __SWITCH__
 SwitchSink native;
 if(!target){if(!native.Init(error)){std::lock_guard<std::mutex>lock(mutex_);fatalError_=error;snapshot_.error=error;snapshot_.loading=false;return;}target=&native;}
#endif
 if(!target){std::lock_guard<std::mutex>lock(mutex_);fatalError_="没有音频输出设备";snapshot_.error=fatalError_;return;}
 PauseGateSink sink(*target,paused_);Engine engine(sink);uint64_t activeRevision=0;
 double resume=-1;bool resumePaused=true;std::string resumeError;uint64_t resumeWindow=0;
 while(running_){
  std::string path;bool play=false,stop=false,doPause=false,pause=true,startPaused=true;double seek=-1,start=0;int volume;uint64_t revision;
  StreamOptions options;std::shared_ptr<StreamStatus>status;
  {std::lock_guard<std::mutex>lock(mutex_);play=pendingPlay_;stop=pendingStop_;pendingStop_=false;
   if(play){path=pendingPath_;pendingPlay_=false;start=startSeconds_;startPaused=startPaused_;}doPause=pendingPause_;pause=paused_.load();pendingPause_=false;
   seek=seek_;seek_=-1;volume=volume_;revision=generation_.load();options=options_;status=status_;}
  engine.Volume(volume);
  if(stop){engine.Stop();activeRevision=revision;resume=-1;resumeError.clear();}
  if(play){activeRevision=revision;resume=start>0?start:-1;resumePaused=startPaused;resumeError.clear();
   options.prefetchBytes=static_cast<size_t>(std::min<uint64_t>(options.cacheBytes,32ull*1024*1024));
   resumeWindow=std::min<uint64_t>(std::min<uint64_t>(options.cacheBytes,512ull*1024*1024)/2,options.prefetchBytes);
   engine.Open(path,&generation_,revision,status,options);
   if(generation_.load()!=revision)continue;
   engine.Pause(paused_.load());
  }
  if(doPause){if(resume<0&&resumeError.empty())engine.Pause(pause);else {resumePaused=pause;paused_=true;}}
  if(seek>=0&&resume<0)engine.Seek(seek);
  if(resume>=0){
   engine.Tick(); // Refresh completed-cache seek capability while still paused.
   if(engine.State().seekable){engine.Seek(resume);resume=-1;paused_=engine.State().error.empty()?resumePaused:true;engine.Pause(paused_.load());}
   else {engine.Pause(true);if(status){auto s=status->Snapshot();if((s.eof||s.cachedEnd>32ull*1024*1024||
      (resumeWindow&&s.cachedEnd>=resumeWindow&&((s.totalBytes>resumeWindow)||(resumeWindow<32ull*1024*1024))))&&!s.cancelled&&s.error.empty()){
    resumeError="当前缓存无法恢复位置（未知时长上限 32MB），重新选曲或按 A 从头播放";resume=-1;paused_=true;
   }}}
  }
  engine.Tick();
  {std::lock_guard<std::mutex>lock(mutex_);if(generation_.load()==activeRevision&&!pendingPlay_&&!pendingStop_&&!pendingPause_&&seek_<0){snapshot_=engine.State();snapshot_.revision=activeRevision;
    if(resume>=0&&snapshot_.error.empty()){snapshot_.loading=true;snapshot_.paused=true;}
    if(!resumeError.empty()&&snapshot_.error.empty()){snapshot_.loading=false;snapshot_.paused=true;snapshot_.error=resumeError;}}}
  std::this_thread::sleep_for(std::chrono::milliseconds(4));
 }
 engine.Stop();
}
}
