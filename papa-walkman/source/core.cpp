#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace papa {
const char* ModeName(Mode mode) {
    switch(mode) {
        case Mode::Sequential: return "顺序播放";
        case Mode::LoopAll: return "列表循环";
        case Mode::RepeatOne: return "单曲循环";
        case Mode::Shuffle: return "随机播放";
    }
    return "列表循环";
}
Settings LoadSettings(const std::string& path) {
    Settings result;
    std::ifstream file(path); std::string line;
    if(!file.is_open()){file.clear();file.open(path+".bak");}
    while (std::getline(file,line)) {
        int value;
        if (std::sscanf(line.c_str(),"volume=%d",&value)==1) result.volume=std::max(0,std::min(100,value));
        else if (std::sscanf(line.c_str(),"mode=%d",&value)==1 && value>=0 && value<=3) result.mode=static_cast<Mode>(value);
    }
    return result;
}
bool SaveSettings(const std::string& path, Settings value) {
    std::string temp=path+".tmp";
    FILE* file=std::fopen(temp.c_str(),"wb"); if(!file) return false;
    bool ok=std::fprintf(file,"volume=%d\nmode=%d\n",std::max(0,std::min(100,value.volume)),static_cast<int>(value.mode))>0;
    ok=std::fclose(file)==0 && ok;
    // Horizon fsdev rename does not replace existing files. Keep a recoverable old copy.
    std::string backup=path+".bak";struct stat st{};bool haveOld=stat(path.c_str(),&st)==0;
    if(ok&&haveOld){
        if(!S_ISREG(st.st_mode))ok=false;
        else{std::remove(backup.c_str());ok=std::rename(path.c_str(),backup.c_str())==0;}
    }
    if(ok){
        ok=std::rename(temp.c_str(),path.c_str())==0;
        if(!ok&&haveOld)std::rename(backup.c_str(),path.c_str());
        if(ok)std::remove(backup.c_str());
    }
    if(!ok) std::remove(temp.c_str());
    return ok;
}
static void Scan(const std::string& root, std::vector<Track>& out, size_t limit, int depth) {
    if (depth<0||out.size()>=limit) return;
    DIR* dir=opendir(root.c_str()); if(!dir) return;
    std::vector<std::string> entries;
    while(auto ent=readdir(dir)) if(ent->d_name[0]!='.') entries.emplace_back(ent->d_name);
    closedir(dir); std::sort(entries.begin(),entries.end());
    for(const auto& name:entries) {
        if(out.size()>=limit) break;
        auto full=root+"/"+name; struct stat st{};
#ifdef _WIN32
        DWORD flags=GetFileAttributesA(full.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES || (flags&FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if(stat(full.c_str(),&st)) continue;
#else
        if(lstat(full.c_str(),&st)||S_ISLNK(st.st_mode)) continue;
#endif
        if(S_ISDIR(st.st_mode)) { if(depth>0) Scan(full,out,limit,depth-1); continue; }
        if(!S_ISREG(st.st_mode)||name.size()<5) continue;
        auto suffix=name.substr(name.size()-4);
        std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(suffix!=".mp3") continue;
        Track track{full,name.substr(0,name.size()-4),"本地音乐"};
        auto sep=track.title.find(" - ");
        if(sep!=std::string::npos && sep>0 && sep+3<track.title.size()) {
            track.artist=track.title.substr(0,sep); track.title.erase(0,sep+3);
        }
        out.push_back(std::move(track));
    }
}
std::vector<Track> ScanLibrary(const std::string& root, size_t limit, int depth) {
    std::vector<Track> out; Scan(root,out,std::min<size_t>(limit,10000),std::min(depth,8));
    std::sort(out.begin(),out.end(),[](const Track&a,const Track&b){return a.title==b.title ? a.path<b.path : a.title<b.title;});
    return out;
}
std::string TimeLabel(double seconds) {
    if(!std::isfinite(seconds)||seconds<0) seconds=0;
    int s=static_cast<int>(std::min(seconds,359999.0)); char text[24];
    std::snprintf(text,sizeof(text),"%02d:%02d",s/60,s%60); return text;
}
void Queue::Reset(size_t count, int current) {
    count_=count; current_=(current>=0&&static_cast<size_t>(current)<count)?current:-1;
    bag_.clear(); history_.clear();
}
void Queue::Select(int index) {
    if(index<0||static_cast<size_t>(index)>=count_) return;
    current_=index; bag_.clear(); history_.clear();
}
void Queue::SetMode(Mode mode) { if(mode_!=mode) {mode_=mode; bag_.clear(); history_.clear();} }
int Queue::Next(bool automatic) {
    if(!count_) return -1;
    if(current_<0) return current_=0;
    if(automatic&&mode_==Mode::RepeatOne) return current_;
    if(automatic&&mode_==Mode::Sequential&&static_cast<size_t>(current_+1)>=count_) return -1;
    if(mode_==Mode::Shuffle&&count_>1) {
        if(bag_.empty()) {
            for(size_t i=0;i<count_;++i) if(static_cast<int>(i)!=current_) bag_.push_back(static_cast<int>(i));
            std::shuffle(bag_.begin(),bag_.end(),random_);
        }
        if(history_.size()>=1000) history_.erase(history_.begin());
        history_.push_back(current_); current_=bag_.back(); bag_.pop_back();
    } else current_=(current_+1)%static_cast<int>(count_);
    return current_;
}
int Queue::Previous() {
    if(!count_) return -1;
    if(mode_==Mode::Shuffle&&!history_.empty()) { bag_.push_back(current_); current_=history_.back(); history_.pop_back(); }
    else current_=current_<=0?static_cast<int>(count_-1):current_-1;
    return current_;
}
void Resampler::Reset(int rate, int channels) {
    rate_=rate>0?rate:48000; channels_=channels==1?1:2; fifo_.clear(); phase_=0;
}
void Resampler::Push(const int16_t* samples, size_t frames) {
    fifo_.reserve(fifo_.size()+frames*2);
    for(size_t i=0;i<frames;++i) {fifo_.push_back(samples[i*channels_]); fifo_.push_back(samples[i*channels_+(channels_==2?1:0)]);}
}
size_t Resampler::Pull(int16_t* stereo, size_t frames, bool eof) {
    size_t count=0, available=fifo_.size()/2;
    while(count<frames) {
        size_t index=static_cast<size_t>(phase_/48000);
        if(index>=available||(!eof&&index+1>=available)) break;
        size_t next=std::min(index+1,available-1); int fraction=static_cast<int>(phase_%48000);
        for(int ch=0;ch<2;++ch) {
            int a=fifo_[index*2+ch], b=fifo_[next*2+ch];
            stereo[count*2+ch]=static_cast<int16_t>(a+(static_cast<int64_t>(b-a)*fraction)/48000);
        }
        ++count; phase_+=static_cast<uint64_t>(rate_);
    }
    size_t consumed=std::min(static_cast<size_t>(phase_/48000),available);
    if(consumed) {fifo_.erase(fifo_.begin(),fifo_.begin()+consumed*2); phase_-=consumed*48000ULL;}
    return count;
}
}
