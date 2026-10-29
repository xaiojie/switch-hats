#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include "decoder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#define MINIMP3_ONLY_MP3
#define MINIMP3_IO_SIZE (32*1024)
#define MINIMP3_SCAN_LIMIT (256*1024)
#include "minimp3_ex.h"
static_assert(MINIMP3_IO_SIZE>MINIMP3_BUF_SIZE,"MP3 IO buffer must hold multiple frames");
namespace papa {
struct Decoder::Impl {
    ByteSource source; bool remote=false; mp3dec_ex_t dec{}; mp3dec_io_t io{}; bool open=false;
    std::string error;uint64_t readFrames=0,finalFrames=0;
    const std::atomic<uint64_t>* cancel=nullptr; uint64_t expected=0;
    bool Cancelled() const {return cancel&&cancel->load()!=expected;}
    static size_t Read(void* out,size_t size,void* user) {
        auto p=static_cast<Impl*>(user);
        size_t filled=0;
        while(filled<size&&!p->Cancelled()){
            size_t n=p->source.Read(static_cast<unsigned char*>(out)+filled,size-filled);
            if(!n)break;
            filled+=n;
        }
        return filled;
    }
    static int Seek(uint64_t offset,void* user) {
        auto p=static_cast<Impl*>(user); if(p->Cancelled()) return -1;
        return p->source.Seek(offset)?0:-1;
    }
};
Decoder::Decoder():p_(new Impl){} Decoder::~Decoder(){Close();}
bool Decoder::Open(const std::string& path,const std::atomic<uint64_t>* cancel,uint64_t expected,std::shared_ptr<StreamStatus> status,StreamOptions options){
    Close(); p_->readFrames=p_->finalFrames=0;p_->error.clear(); p_->cancel=cancel; p_->expected=expected;
    if(p_->Cancelled()){p_->error="操作已取消";return false;}
    // Retain the opening IO window while minimp3 seeks back to its first frame.
    options.cacheBytes=std::max<uint64_t>(options.cacheBytes,4*MINIMP3_IO_SIZE);
    if(!p_->source.Open(path,cancel,expected,status,options)){p_->error=p_->source.Status().error;return false;}
    p_->remote=p_->source.IsRemote();
    p_->io={Impl::Read,p_.get(),Impl::Seek,p_.get()};
    int result=mp3dec_ex_open_cb(&p_->dec,&p_->io,MP3D_SEEK_TO_SAMPLE|(p_->remote?MP3D_DO_NOT_SCAN:0));
    if(result!=0||p_->Cancelled()||p_->dec.info.hz<8000||p_->dec.info.hz>192000||
       (p_->dec.info.channels!=1&&p_->dec.info.channels!=2)||(!p_->remote&&!p_->dec.samples)){
        mp3dec_ex_close(&p_->dec); p_->source.Close();
        p_->error=p_->source.Status().error.empty()?"这首 MP3 无法播放，请换一首":p_->source.Status().error; return false;
    }
    p_->open=true; return true;
}
void Decoder::Close(){
    if(p_->open)mp3dec_ex_close(&p_->dec);
    p_->source.Close();p_->open=false;p_->dec={};
}
bool Decoder::Seek(double seconds){
    if(!p_->open)return false;
    if(!std::isfinite(seconds))seconds=0;
    seconds=std::max(0.0,DurationKnown()?std::min(seconds,Duration()):seconds);
    if(p_->remote){
        auto st=p_->source.Status();if(st.cachedStart||!st.eof)return false;
        if(!DurationKnown())return false;
        mp3dec_ex_close(&p_->dec);p_->dec={};
        if(mp3dec_ex_open_cb(&p_->dec,&p_->io,MP3D_SEEK_TO_SAMPLE|MP3D_DO_NOT_SCAN)){p_->error="Cannot reopen cached MP3";return false;}
        p_->readFrames=0;uint64_t remaining=static_cast<uint64_t>(seconds*Rate());int16_t discard[4096];
        while(remaining){size_t n=Read(discard,static_cast<size_t>(std::min<uint64_t>(remaining,2048)));if(!n)break;remaining-=n;}
        return p_->error.empty();
    }
    uint64_t sample=static_cast<uint64_t>(seconds*Rate())*static_cast<uint64_t>(Channels());
    if(mp3dec_ex_seek(&p_->dec,std::min<uint64_t>(sample,p_->dec.samples))!=0){p_->error="跳转进度失败";return false;}
    p_->error.clear();return true;
}
size_t Decoder::Read(int16_t* samples,size_t frames){
    if(!p_->open)return 0;
    size_t n=mp3dec_ex_read(&p_->dec,samples,frames*Channels());
    if(!n&&p_->remote)p_->source.Finish();
    auto status=p_->source.Status();if(!status.error.empty())p_->error=status.error;
    if(status.cancelled)p_->error="Request cancelled";
    if(p_->dec.last_error&&p_->error.empty())p_->error="MP3 解码中断，请换一首";
    p_->readFrames+=n/static_cast<size_t>(Channels());
    if(!n&&status.eof&&p_->error.empty())p_->finalFrames=p_->readFrames;
    return n/static_cast<size_t>(Channels());
}
int Decoder::Rate()const{return p_->open?p_->dec.info.hz:0;}
int Decoder::Channels()const{return p_->open?p_->dec.info.channels:0;}
double Decoder::Duration()const{return p_->open?(p_->dec.samples?static_cast<double>(p_->dec.samples)/Channels():static_cast<double>(p_->finalFrames))/Rate():0;}
bool Decoder::DurationKnown()const{return p_->open&&(p_->dec.samples>0||p_->finalFrames>0);}
bool Decoder::Seekable()const{auto s=p_->source.Status();return p_->open&&(!p_->remote||(s.eof&&!s.cachedStart&&DurationKnown()));}
bool Decoder::Buffering()const{return p_->source.Status().buffering;}
std::string Decoder::Error()const{return p_->error;}
}
