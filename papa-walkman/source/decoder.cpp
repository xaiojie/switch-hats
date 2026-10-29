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
#define MINIMP3_SCAN_LIMIT (256*1024)
#include "minimp3_ex.h"
namespace papa {
struct Decoder::Impl {
    FILE* file=nullptr; mp3dec_ex_t dec{}; mp3dec_io_t io{}; bool open=false;
    std::string error;
    const std::atomic<uint64_t>* cancel=nullptr; uint64_t expected=0;
    bool Cancelled() const {return cancel&&cancel->load()!=expected;}
    static size_t Read(void* out,size_t size,void* user) {
        auto p=static_cast<Impl*>(user);
        return p->Cancelled()?0:std::fread(out,1,size,p->file);
    }
    static int Seek(uint64_t offset,void* user) {
        auto p=static_cast<Impl*>(user); if(p->Cancelled()) return -1;
#ifdef _WIN32
        return _fseeki64(p->file,static_cast<int64_t>(offset),SEEK_SET);
#else
        return fseeko(p->file,static_cast<off_t>(offset),SEEK_SET);
#endif
    }
};
Decoder::Decoder():p_(new Impl){} Decoder::~Decoder(){Close();}
bool Decoder::Open(const std::string& path,const std::atomic<uint64_t>* cancel,uint64_t expected){
    Close(); p_->error.clear(); p_->cancel=cancel; p_->expected=expected;
    if(p_->Cancelled()){p_->error="操作已取消";return false;}
    p_->file=std::fopen(path.c_str(),"rb");
    if(!p_->file){p_->error="无法读取文件，请检查 SD 卡";return false;}
    p_->io={Impl::Read,p_.get(),Impl::Seek,p_.get()};
    int result=mp3dec_ex_open_cb(&p_->dec,&p_->io,MP3D_SEEK_TO_SAMPLE);
    if(result!=0||p_->Cancelled()||p_->dec.info.hz<8000||p_->dec.info.hz>192000||
       (p_->dec.info.channels!=1&&p_->dec.info.channels!=2)||!p_->dec.samples){
        mp3dec_ex_close(&p_->dec); std::fclose(p_->file);p_->file=nullptr;
        p_->error="这首 MP3 无法播放，请换一首"; return false;
    }
    p_->open=true; return true;
}
void Decoder::Close(){
    if(p_->open)mp3dec_ex_close(&p_->dec);
    if(p_->file)std::fclose(p_->file);
    p_->file=nullptr;p_->open=false;p_->dec={};
}
bool Decoder::Seek(double seconds){
    if(!p_->open)return false;
    if(!std::isfinite(seconds))seconds=0;
    seconds=std::max(0.0,std::min(seconds,Duration()));
    uint64_t sample=static_cast<uint64_t>(seconds*Rate())*static_cast<uint64_t>(Channels());
    if(mp3dec_ex_seek(&p_->dec,std::min<uint64_t>(sample,p_->dec.samples))!=0){p_->error="跳转进度失败";return false;}
    p_->error.clear();return true;
}
size_t Decoder::Read(int16_t* samples,size_t frames){
    if(!p_->open)return 0;
    size_t n=mp3dec_ex_read(&p_->dec,samples,frames*Channels());
    if(p_->dec.last_error)p_->error="MP3 解码中断，请换一首";
    return n/static_cast<size_t>(Channels());
}
int Decoder::Rate()const{return p_->open?p_->dec.info.hz:0;}
int Decoder::Channels()const{return p_->open?p_->dec.info.channels:0;}
double Decoder::Duration()const{return p_->open?static_cast<double>(p_->dec.samples)/Channels()/Rate():0;}
std::string Decoder::Error()const{return p_->error;}
}
