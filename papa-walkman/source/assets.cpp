#include "assets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cerrno>
#include <set>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
namespace papa {
namespace {
#ifdef _WIN32
std::wstring Wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,nullptr,0);if(n<=0)return {};std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,&w[0],n);w.resize(n-1);return w;}
std::string Narrow(const wchar_t*s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string t(n,0);WideCharToMultiByte(CP_UTF8,0,s,-1,&t[0],n,nullptr,nullptr);t.resize(n-1);return t;}
#endif
bool Remove(const std::string&p){
#ifdef _WIN32
return DeleteFileW(Wide(p).c_str())!=0;
#else
return std::remove(p.c_str())==0;
#endif
}
bool Move(const std::string&a,const std::string&b){
#ifdef _WIN32
return MoveFileExW(Wide(a).c_str(),Wide(b).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
return std::rename(a.c_str(),b.c_str())==0;
#endif
}
bool Directory(const std::string&p){
#ifdef _WIN32
DWORD a=GetFileAttributesW(Wide(p).c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
struct stat s{};return lstat(p.c_str(),&s)==0&&S_ISDIR(s.st_mode)&&!S_ISLNK(s.st_mode);
#endif
}
struct Entry{std::string path;uint64_t bytes=0,modified=0;bool stream=false;};
bool Managed(const std::string&name,const std::string&dir){
    if(dir=="streams")return name.compare(0,12,"papa-stream-")==0&&name.size()>16&&name.substr(name.size()-4)==".tmp";
    if(dir=="assets")return name.compare(0,11,"papa-asset-")==0&&((name.size()==31&&name.substr(27)==".bin")||(name.size()==35&&name.substr(27)==".bin.tmp"))&&std::all_of(name.begin()+11,name.begin()+27,[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
    auto dot=name.find('.');if(dot!=16)return false;
    return std::all_of(name.begin(),name.begin()+16,[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
std::vector<Entry> Entries(const std::string&root,bool streamsOnly=false){
    std::vector<Entry>result;
    for(const auto*folder:{"metadata","assets","streams"}){
        if(streamsOnly&&std::string(folder)!="streams")continue;
        std::string dir=root+"/"+folder;if(!Directory(dir))continue;
#ifdef _WIN32
        WIN32_FIND_DATAW data{};HANDLE h=FindFirstFileW(Wide(dir+"/*").c_str(),&data);if(h==INVALID_HANDLE_VALUE)continue;
        do{auto name=Narrow(data.cFileName);if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||!Managed(name,folder))continue;
            result.push_back({dir+"/"+name,(uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow,(uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime,std::string(folder)=="streams"});}while(FindNextFileW(h,&data));FindClose(h);
#else
        DIR*d=opendir(dir.c_str());if(!d)continue;
        while(auto item=readdir(d)){std::string name=item->d_name;if(!Managed(name,folder))continue;struct stat s{};auto path=dir+"/"+name;if(lstat(path.c_str(),&s)||!S_ISREG(s.st_mode))continue;result.push_back({path,static_cast<uint64_t>(s.st_size),static_cast<uint64_t>(s.st_mtime),std::string(folder)=="streams"});}closedir(d);
#endif
    }
    return result;
}
std::vector<uint8_t>ReadFile(const std::string&p,size_t max){
    FILE*f=OpenUtf8File(p,"rb");if(!f)return {};std::vector<uint8_t>out;uint8_t chunk[8192];size_t n;
    while((n=std::fread(chunk,1,sizeof(chunk),f))){if(out.size()>max||n>max-out.size()){std::fclose(f);return {};}out.insert(out.end(),chunk,chunk+n);}
    bool error=std::ferror(f)!=0;std::fclose(f);return error?std::vector<uint8_t>{}:out;
}
std::string Key(const std::string&s){uint64_t h=14695981039346656037ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}char out[24];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(h));return out;}
bool Cancelled(const std::atomic<uint64_t>*p,uint64_t v){return p&&p->load()!=v;}

void RestoreDerived(Track&track,MediaCache&cache){
    if(track.source!=SourceKind::SD||track.path.empty())return;
    const std::string prefix=cache.MetadataDirectory()+"/";
    bool missing=false;
    for(auto*ref:{&track.cover,&track.lyrics})if(ref->compare(0,prefix.size(),prefix)==0){FILE*f=OpenUtf8File(*ref,"rb");if(f)std::fclose(f);else {ref->clear();missing=true;}}
    if(missing)cache.Enrich(track);
}
}
std::shared_ptr<CoverImage>DecodeCover(const std::vector<uint8_t>&bytes,int maximumEdge){
    if(bytes.empty()||bytes.size()>4*1024*1024||maximumEdge<1||maximumEdge>1024)return {};
    static std::mutex decoding;std::lock_guard<std::mutex>decodeLock(decoding);
    int w=0,h=0,ch=0;
    if(!stbi_info_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&ch)||w<=0||h<=0||uint64_t(w)*h>8*1024*1024)return {};
    unsigned char*decoded=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&ch,4);if(!decoded)return {};
    auto out=std::make_shared<CoverImage>();double scale=std::min(1.0,maximumEdge/double(std::max(w,h)));
    out->width=std::max(1,static_cast<int>(w*scale));out->height=std::max(1,static_cast<int>(h*scale));out->pixels.resize(static_cast<size_t>(out->width)*out->height);
    for(int y=0;y<out->height;++y)for(int x=0;x<out->width;++x){
        double fx=(x+.5)*w/out->width-.5,fy=(y+.5)*h/out->height-.5;int x0=std::max(0,std::min(w-1,static_cast<int>(std::floor(fx)))),y0=std::max(0,std::min(h-1,static_cast<int>(std::floor(fy))));
        int x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);double ax=std::max(0.0,fx-x0),ay=std::max(0.0,fy-y0);uint32_t pixel=0;
        for(int c=0;c<4;++c){double top=decoded[(y0*w+x0)*4+c]*(1-ax)+decoded[(y0*w+x1)*4+c]*ax,bottom=decoded[(y1*w+x0)*4+c]*(1-ax)+decoded[(y1*w+x1)*4+c]*ax;pixel|=uint32_t(std::max(0.0,std::min(255.0,top*(1-ay)+bottom*ay)))<<(c*8);}
        out->pixels[static_cast<size_t>(y)*out->width+x]=pixel;
    }
    stbi_image_free(decoded);return out;
}
bool CreateDirectories(const std::string&path){
    if(path.empty())return false;
    std::string normalized=path;std::replace(normalized.begin(),normalized.end(),'\\','/');
    for(size_t i=0;i<=normalized.size();++i){if(i!=normalized.size()&&normalized[i]!='/')continue;std::string part=normalized.substr(0,i);if(part.empty()||part.back()==':')continue;if(Directory(part))continue;
#ifdef _WIN32
        if(!CreateDirectoryW(Wide(part).c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
#else
        if(mkdir(part.c_str(),0777)&&errno!=EEXIST)return false;
#endif
        if(!Directory(part))return false;
    }
    return Directory(normalized);
}
MediaCache::MediaCache(std::string root,uint64_t limit):root_(std::move(root)),limit_(std::max<uint64_t>(4096,limit)){
    std::replace(root_.begin(),root_.end(),'\\','/');
    while(!root_.empty()&&(root_.back()=='/'||root_.back()=='\\'))root_.pop_back();
    CreateDirectories(MetadataDirectory());CreateDirectories(StreamDirectory());CreateDirectories(root_+"/assets");
}
CacheUsage MediaCache::Usage()const{std::lock_guard<std::mutex>lock(usageMutex_);return usage_;}
void MediaCache::PublishLocked(){std::lock_guard<std::mutex>lock(usageMutex_);usage_={total_,static_cast<uint64_t>(index_.size()),ready_};}
bool MediaCache::ManagedPath(const std::string&path)const{
    for(const auto*folder:{"metadata","assets","streams"}){auto prefix=root_+"/"+folder+"/";
        if(path.compare(0,prefix.size(),prefix))continue;auto name=path.substr(prefix.size());
        return name.find_first_of("/\\")==std::string::npos&&Managed(name,folder)&&Directory(root_+"/"+folder);
    }return false;
}
void MediaCache::RecordLocked(const std::string&path){
    auto old=index_.find(path);if(old!=index_.end()){total_-=old->second.bytes;index_.erase(old);}
    streams_.erase(path);if(!ready_)changed_.insert(path);
    Indexed e;bool exists=false;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d{};if(GetFileAttributesExW(Wide(path).c_str(),GetFileExInfoStandard,&d)&&!(d.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){e.bytes=(uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;e.modified=(uint64_t(d.ftLastWriteTime.dwHighDateTime)<<32)|d.ftLastWriteTime.dwLowDateTime;exists=true;}
#else
    struct stat d{};if(!lstat(path.c_str(),&d)&&S_ISREG(d.st_mode)){e.bytes=d.st_size;e.modified=d.st_mtime;exists=true;}
#endif
    if(exists){e.stream=path.compare(0,StreamDirectory().size()+1,StreamDirectory()+"/")==0;index_[path]=e;total_+=e.bytes;if(e.stream)streams_.insert(path);}
}
void MediaCache::RecordAsset(const std::string&path){if(!ManagedPath(path))return;std::lock_guard<std::mutex>lock(mutex_);RecordLocked(path);PublishLocked();}
void MediaCache::Warm(){
    std::lock_guard<std::mutex>warm(warmMutex_);{std::lock_guard<std::mutex>lock(mutex_);if(ready_)return;}
    auto files=Entries(root_);std::map<std::string,Indexed>discovered;
    for(const auto&e:files)discovered[e.path]={e.bytes,e.modified,e.stream};
    std::lock_guard<std::mutex>lock(mutex_);
    // Updates while enumeration ran, including deletions, supersede its stale observations.
    for(const auto&p:changed_){auto it=index_.find(p);if(it==index_.end())discovered.erase(p);else discovered[p]=it->second;}
    index_.swap(discovered);total_=0;streams_.clear();for(const auto&e:index_){total_+=e.second.bytes;if(e.second.stream)streams_.insert(e.first);}
    changed_.clear();ready_=true;PublishLocked();
}
void MediaCache::RefreshStreams(bool force){
    std::lock_guard<std::mutex>refresh(streamMutex_);auto now=std::chrono::steady_clock::now();
    if(!force&&now-streamRefresh_<std::chrono::seconds(1))return;
    auto files=Entries(root_,true);streamRefresh_=now;std::lock_guard<std::mutex>lock(mutex_);
    for(const auto&p:streams_){auto it=index_.find(p);if(it!=index_.end()){total_-=it->second.bytes;index_.erase(it);}if(!ready_)changed_.insert(p);}
    streams_.clear();
    for(const auto&e:files){index_[e.path]={e.bytes,e.modified,true};total_+=e.bytes;streams_.insert(e.path);if(!ready_)changed_.insert(e.path);}PublishLocked();
}
bool MediaCache::TrimLocked(uint64_t target,const std::set<std::string>&keep){
    if(!ready_)return false;if(total_<=target)return true;
    std::vector<Entry>files;for(const auto&e:index_)if(!e.second.stream&&!keep.count(e.first))files.push_back({e.first,e.second.bytes,e.second.modified,false});
    std::sort(files.begin(),files.end(),[](const Entry&a,const Entry&b){return a.modified==b.modified?a.path<b.path:a.modified<b.modified;});
    for(const auto&e:files){if(total_<=target)break;if(Remove(e.path)){total_-=e.bytes;index_.erase(e.path);}else RecordLocked(e.path);}
    PublishLocked();return total_<=target;
}
void MediaCache::Enrich(Track&track){
    std::lock_guard<std::mutex>write(writeMutex_);
    bool room=Reserve(5ull*1024*1024,{track.cover,track.lyrics});
    EnrichTrack(track,room?MetadataDirectory():std::string{});
    RecordAsset(track.cover);RecordAsset(track.lyrics);Prune({track.cover,track.lyrics});
}
void MediaCache::SetLimit(uint64_t v){std::lock_guard<std::mutex>lock(mutex_);limit_=std::max<uint64_t>(4096,v);}
void MediaCache::SetPins(const std::vector<std::string>&paths){std::lock_guard<std::mutex>lock(mutex_);pins_.clear();for(const auto&p:paths)if(!p.empty())pins_.insert(AssetPath(p));}
std::string MediaCache::AssetPath(const std::string&reference)const{
    if(reference.compare(0,7,"http://")&&reference.compare(0,8,"https://"))return reference;
    return root_+"/assets/papa-asset-"+Key(reference)+".bin";
}
bool MediaCache::Reserve(uint64_t bytes,const std::vector<std::string>&pinned){
    RefreshStreams(false);
    std::lock_guard<std::mutex>lock(mutex_);if(bytes>limit_)return false;auto keep=pins_;for(const auto&p:pinned)keep.insert(AssetPath(p));return TrimLocked(limit_-bytes,keep);
}
bool MediaCache::Prune(const std::vector<std::string>&pinned){
    std::lock_guard<std::mutex>lock(mutex_);auto keep=pins_;for(const auto&p:pinned)keep.insert(AssetPath(p));return TrimLocked(limit_,keep);
}
void MediaCache::ClearIdle(const std::vector<std::string>&pinned){
    std::lock_guard<std::mutex>lock(mutex_);if(!ready_)return;auto keep=pins_;for(const auto&p:pinned)keep.insert(AssetPath(p));
    std::vector<std::string>paths;for(const auto&e:index_)if(!e.second.stream&&!keep.count(e.first))paths.push_back(e.first);
    for(const auto&p:paths){if(Remove(p)){total_-=index_.at(p).bytes;index_.erase(p);}else RecordLocked(p);}PublishLocked();
}
void MediaCache::RemoveStartupOrphans(){for(const auto&e:Entries(root_,true))if(Remove(e.path))RecordAsset(e.path);}
std::vector<uint8_t>MediaCache::ReadAsset(const std::string&reference,size_t maximum,const std::atomic<uint64_t>*cancel,uint64_t expected,bool refresh){
    if(reference.empty()||Cancelled(cancel,expected))return {};
    if(reference.compare(0,7,"http://")&&reference.compare(0,8,"https://"))return ReadFile(reference,maximum);
    std::string cached=AssetPath(reference);
    if(!refresh){std::lock_guard<std::mutex>lock(mutex_);auto bytes=ReadFile(cached,maximum);if(!bytes.empty())return bytes;}
    auto result=FetchUrl(reference,maximum,cancel,expected);if(result.cancelled||Cancelled(cancel,expected))return {};
    if(!result.error.empty()){std::lock_guard<std::mutex>lock(mutex_);return ReadFile(cached,maximum);}
    {std::lock_guard<std::mutex>write(writeMutex_);
        RefreshStreams();
        std::lock_guard<std::mutex>lock(mutex_);auto keep=pins_;keep.insert(cached);
        // Retain the old URL file until replacement is complete; count both at peak.
        if(result.bytes.size()<=limit_&&TrimLocked(limit_-result.bytes.size(),keep)){
            auto temporary=cached+".tmp";FILE*f=OpenUtf8File(temporary,"wb");if(f){bool ok=std::fwrite(result.bytes.data(),1,result.bytes.size(),f)==result.bytes.size();ok=std::fclose(f)==0&&ok;if(ok&&!Cancelled(cancel,expected)){if(!Move(temporary,cached))Remove(temporary);}else Remove(temporary);RecordLocked(temporary);RecordLocked(cached);PublishLocked();}
        }
    }
    return result.bytes;
}
ArtworkLoader::ArtworkLoader(MediaCache&c):cache_(c),thread_([this]{Run();}){}
ArtworkLoader::~ArtworkLoader(){generation_.fetch_add(1);{std::lock_guard<std::mutex>lock(mutex_);running_=false;}changed_.notify_all();if(thread_.joinable())thread_.join();}
void ArtworkLoader::Request(const Track&track,bool refresh){std::lock_guard<std::mutex>lock(mutex_);generation_.fetch_add(1);requested_=track;pending_=true;refresh_=refresh;snapshot_={};snapshot_.trackId=track.id;snapshot_.loading=true;changed_.notify_one();}
ArtworkSnapshot ArtworkLoader::Snapshot(){std::lock_guard<std::mutex>lock(mutex_);return snapshot_;}
void ArtworkLoader::Run(){
    for(;;){Track track;bool refresh;uint64_t version;
        {std::unique_lock<std::mutex>lock(mutex_);changed_.wait(lock,[this]{return pending_||!running_;});if(!running_)return;track=requested_;refresh=refresh_;pending_=false;version=generation_.load();}
        ArtworkSnapshot value;value.trackId=track.id;
        RestoreDerived(track,cache_);
        if(!track.cover.empty()){auto bytes=cache_.ReadAsset(track.cover,4*1024*1024,&generation_,version,refresh);if(!Cancelled(&generation_,version))value.cover=DecodeCover(bytes);if(!value.cover)value.note="封面暂不可用";}
        if(!Cancelled(&generation_,version)&&!track.lyrics.empty()){auto bytes=cache_.ReadAsset(track.lyrics,1024*1024,&generation_,version,refresh);if(!bytes.empty())value.lyrics=std::make_shared<Lyrics>(ParseLyrics(std::string(bytes.begin(),bytes.end())));}
        cache_.Prune({track.cover,track.lyrics});
        {std::lock_guard<std::mutex>lock(mutex_);if(running_&&generation_.load()==version&&!pending_)snapshot_=std::move(value);}
    }
}
ThumbnailLoader::ThumbnailLoader(MediaCache&cache):cache_(cache),thread_([this]{Run();}){}
ThumbnailLoader::~ThumbnailLoader(){generation_.fetch_add(1);{std::lock_guard<std::mutex>lock(mutex_);running_=false;}changed_.notify_all();if(thread_.joinable())thread_.join();}
std::shared_ptr<CoverImage>ThumbnailLoader::Get(const Track&track){
    if(track.cover.empty())return {};
    std::lock_guard<std::mutex>lock(mutex_);auto found=ready_.find(track.cover);
    if(found!=ready_.end()){found->second.second=++serial_;return found->second.first;}
    if(queued_.size()<24&&pending_.insert(track.cover).second){queued_.push_back(track);changed_.notify_one();}
    return {};
}
void ThumbnailLoader::Clear(bool refresh){std::lock_guard<std::mutex>lock(mutex_);generation_.fetch_add(1);queued_.clear();pending_.clear();ready_.clear();refresh_=refresh;}
void ThumbnailLoader::Run(){
    for(;;){Track track;uint64_t version;bool refresh;
        {std::unique_lock<std::mutex>lock(mutex_);changed_.wait(lock,[this]{return !running_||!queued_.empty();});if(!running_)return;track=std::move(queued_.front());queued_.pop_front();version=generation_.load();refresh=refresh_;}
        std::string key=track.cover;RestoreDerived(track,cache_);auto bytes=cache_.ReadAsset(track.cover,4*1024*1024,&generation_,version,refresh);
        std::shared_ptr<CoverImage>image;if(!Cancelled(&generation_,version))image=DecodeCover(bytes,128);
        cache_.Prune({track.cover,track.lyrics});
        {std::lock_guard<std::mutex>lock(mutex_);if(generation_.load()!=version)continue;pending_.erase(key);ready_[key]={std::move(image),++serial_};
            while(ready_.size()>32){auto oldest=std::min_element(ready_.begin(),ready_.end(),[](const decltype(ready_)::value_type&a,const decltype(ready_)::value_type&b){return a.second.second<b.second.second;});ready_.erase(oldest);}
        }
    }
}
}
