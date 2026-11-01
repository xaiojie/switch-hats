#include "assets.hpp"
#include "fslock.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
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
return papa::RemoveLocked(p.c_str())==0;
#endif
}
bool Move(const std::string&a,const std::string&b){
#ifdef _WIN32
return MoveFileExW(Wide(a).c_str(),Wide(b).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
return papa::RenameLocked(a.c_str(),b.c_str())==0;
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
    while((n=std::fread(chunk,1,sizeof(chunk),f))){if(out.size()>max||n>max-out.size()){papa::CloseLocked(f);return {};}out.insert(out.end(),chunk,chunk+n);}
    bool error=std::ferror(f)!=0;papa::CloseLocked(f);return error?std::vector<uint8_t>{}:out;
}
std::string Key(const std::string&s){uint64_t h=14695981039346656037ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}char out[24];std::snprintf(out,sizeof(out),"%016llx",static_cast<unsigned long long>(h));return out;}
bool Cancelled(const std::atomic<uint64_t>*p,uint64_t v){return p&&p->load()!=v;}

void RestoreDerived(Track&track,MediaCache&cache){
    if(track.path.empty())return;
    const std::string prefix=cache.MetadataDirectory()+"/";
    bool missing=false;
    for(auto*ref:{&track.cover,&track.lyrics})if(ref->compare(0,prefix.size(),prefix)==0){FILE*f=OpenUtf8File(*ref,"rb");if(f)papa::CloseLocked(f);else {ref->clear();missing=true;}}
    if(missing&&track.source==SourceKind::SD)cache.Enrich(track);
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
        if(path.compare(0,prefix.size(),prefix))continue;
        auto name=path.substr(prefix.size());
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
    if(!ready_)return false;
    if(total_<=target)return true;
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
void MediaCache::SetLimit(uint64_t v){limit_.store(std::max<uint64_t>(4096,v));}
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
    if(!refresh){auto bytes=ReadFile(cached,maximum);if(!bytes.empty())return bytes;}
    auto result=FetchUrl(reference,maximum,cancel,expected);if(result.cancelled||Cancelled(cancel,expected))return {};
    if(!result.error.empty())return ReadFile(cached,maximum);
    Store(cached,result.bytes,{},cancel,expected);
    return result.bytes;
}
namespace {
constexpr size_t CloudTagLimit=16*1024*1024,CloudTextLimit=1024*1024;
bool Fresh(const std::string& path,int ageSeconds){
 struct stat info{};return stat(path.c_str(),&info)==0 && info.st_mtime<=std::time(nullptr) && std::time(nullptr)-info.st_mtime<ageSeconds;
}
bool Readable(const std::string& path){FILE* f=OpenUtf8File(path,"rb");if(!f)return false;papa::CloseLocked(f);return true;}
bool GoodText(const std::string& text){return !text.empty()&&text.find("\xef\xbf\xbd")==std::string::npos;}
void FillMetadata(Track& track,const Track& metadata){
 if(track.title.empty()&&GoodText(metadata.title))track.title=metadata.title;
 if((track.artist.empty()||track.artist=="未知歌手")&&GoodText(metadata.artist))track.artist=metadata.artist;
 if((track.album.empty()||track.album=="未知专辑")&&GoodText(metadata.album))track.album=metadata.album;
 if(track.albumArtist.empty()&&GoodText(metadata.albumArtist))track.albumArtist=metadata.albumArtist;
 if(track.duration<=0&&std::isfinite(metadata.duration)&&metadata.duration>0)track.duration=metadata.duration;
 if(!track.trackNumber)track.trackNumber=metadata.trackNumber;
 if(!track.discNumber)track.discNumber=metadata.discNumber;
 if(track.cover.empty())track.cover=metadata.cover;
 if(track.lyrics.empty())track.lyrics=metadata.lyrics;
}
std::string LyricText(const Metadata& m){
 if(m.lyrics.lines.empty())return m.rawLyrics.size()<=CloudTextLimit?m.rawLyrics:std::string{};
 std::string out="[offset:"+std::to_string(m.lyrics.offset*1000)+"]\n";
 for(const auto& line:m.lyrics.lines){char timestamp[64];std::snprintf(timestamp,sizeof(timestamp),"[%02d:%06.3f]",int(line.seconds/60),std::fmod(line.seconds,60));
  if(out.size()+line.text.size()+std::strlen(timestamp)+1>CloudTextLimit)return {};
  out+=timestamp;out+=line.text;out+='\n';}
 return out;
}
std::vector<uint8_t> Bytes(const std::string& text){return {text.begin(),text.end()};}
}
bool MediaCache::Store(const std::string& path,const std::vector<uint8_t>& bytes,const std::vector<std::string>& pinned,const std::atomic<uint64_t>*cancel,uint64_t expected){
 if(!ManagedPath(path)||bytes.size()>CloudTagLimit||Cancelled(cancel,expected))return false;
 std::lock_guard<std::mutex>write(writeMutex_);
 auto keep=pinned;keep.push_back(path);if(!Reserve(bytes.size(),keep)||Cancelled(cancel,expected))return false;
 const auto temporary=path+".tmp";FILE* f=OpenUtf8File(temporary,"wb");if(!f)return false;
 bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();ok=std::fflush(f)==0&&ok;ok=papa::CloseLocked(f)==0&&ok;
 if(ok&&!Cancelled(cancel,expected))ok=Move(temporary,path);else ok=false;
 if(!ok)Remove(temporary);
 RecordAsset(temporary);RecordAsset(path);return ok;
}
void MediaCache::ResolveCloud(Track& track,const std::atomic<uint64_t>*cancel,uint64_t expected,bool refresh){
 if(track.source!=SourceKind::Cloud||Cancelled(cancel,expected))return;
 if(!track.title.empty()&&!track.artist.empty()&&!track.album.empty()&&track.album!="未知专辑"&&track.duration>0&&!track.cover.empty()&&!track.lyrics.empty())return;
 const auto prefix=MetadataDirectory()+"/"+Key(track.path+"/remote-v1");
 const auto record=prefix+".remote.m3u";
 if(!refresh&&Fresh(record,24*60*60)){
  auto bytes=ReadFile(record,128*1024);std::string error;auto saved=ParseCatalog(std::string(bytes.begin(),bytes.end()),"","cache",error);
  if(saved.size()==1&&saved[0].id==track.id&&saved[0].path==track.path && (saved[0].cover.empty()||Readable(saved[0].cover)) && (saved[0].lyrics.empty()||Readable(saved[0].lyrics))){FillMetadata(track,saved[0]);return;}
 }
 auto response=FetchPrefix(track.path,65536,cancel,expected);
 if(!response.error.empty()||Cancelled(cancel,expected))return;
 if(response.bytes.size()>=10&&!std::memcmp(response.bytes.data(),"ID3",3)){
  const auto* h=response.bytes.data()+6;
  if(!(h[0]&128)&&!(h[1]&128)&&!(h[2]&128)&&!(h[3]&128)){
   size_t size=(size_t(h[0])<<21)|(size_t(h[1])<<14)|(size_t(h[2])<<7)|h[3];
   if(size<=CloudTagLimit&&size+10>response.bytes.size())response=FetchPrefix(track.path,size+10,cancel,expected);
  }
 }
 if(!response.error.empty()||Cancelled(cancel,expected))return;
 auto m=ReadMetadataBytes(response.bytes);Track metadata;metadata.source=SourceKind::Cloud;metadata.path=track.path;metadata.id=track.id;metadata.sourceId=track.sourceId;
 metadata.title=m.title;metadata.artist=m.artist;metadata.album=m.album;metadata.albumArtist=m.albumArtist;metadata.duration=m.duration;metadata.trackNumber=m.track;metadata.discNumber=m.disc;
 std::vector<std::string>keep{record,track.cover,track.lyrics};
 if(!m.cover.empty()){auto path=prefix+(m.coverMime=="image/png"?".png":".jpg");if(Store(path,m.cover,keep,cancel,expected)){metadata.cover=path;keep.push_back(path);}}
 auto lyrics=LyricText(m);
 if(!lyrics.empty()){auto path=prefix+".lrc";if(Store(path,Bytes(lyrics),keep,cancel,expected)){metadata.lyrics=path;keep.push_back(path);}}
 if(Cancelled(cancel,expected))return;
 Store(record,Bytes(ExportPlaylist({"remote","remote",{metadata}})),keep,cancel,expected);
 FillMetadata(track,metadata);
}
void MediaCache::ResolveLyrics(Track& track,const std::string& service,const std::atomic<uint64_t>*cancel,uint64_t expected,bool refresh){
 if(!track.lyrics.empty()||Cancelled(cancel,expected))return;
 auto url=ExactLyricsUrl(service,track);if(url.empty()||url.size()>8192)return;
 const auto prefix=MetadataDirectory()+"/"+Key(url+"/exact-v1");
 const auto path=prefix+".lrc",miss=prefix+".miss",retry=prefix+".retry";
 if(!refresh){
  if(Fresh(path,30*24*60*60)&&!ReadFile(path,CloudTextLimit).empty()){track.lyrics=path;auto bytes=ReadFile(prefix+".duration",128);std::string text(bytes.begin(),bytes.end());char* end=nullptr;double duration=std::strtod(text.c_str(),&end);if(track.duration<=0&&end!=text.c_str()&&!*end&&std::isfinite(duration)&&duration>0&&duration<=86400)track.duration=duration;return;}
  if(Fresh(miss,24*60*60)||Fresh(retry,5*60))return;
 }
 auto response=FetchUrl(url,2*CloudTextLimit+65536,cancel,expected,6000);
 if(response.cancelled||Cancelled(cancel,expected))return;
 if(!response.error.empty()){
  Store(response.httpCode==404?miss:retry,{'1'}, {track.cover},cancel,expected);return;
 }
 auto exact=ParseExactLyrics(std::string(response.bytes.begin(),response.bytes.end()),track);
 if(!exact.matched||exact.text.empty()){Store(miss,{'1'},{track.cover},cancel,expected);return;}
 if(Store(path,Bytes(exact.text),{track.cover},cancel,expected)){track.lyrics=path;Store(prefix+".duration",Bytes(std::to_string(exact.duration)),{track.cover,path},cancel,expected);if(track.duration<=0)track.duration=exact.duration;}
}
ArtworkLoader::ArtworkLoader(MediaCache&c,std::string service):cache_(c),lyricsService_(std::move(service)),thread_([this]{Run();}){}
ArtworkLoader::~ArtworkLoader(){generation_.fetch_add(1);{std::lock_guard<std::mutex>lock(mutex_);running_=false;}changed_.notify_all();if(thread_.joinable())thread_.join();}
void ArtworkLoader::Request(const Track&track,bool refresh){std::lock_guard<std::mutex>lock(mutex_);generation_.fetch_add(1);requested_=track;pending_=true;refresh_=refresh;snapshot_={};snapshot_.trackId=track.id;snapshot_.trackPath=track.path;snapshot_.loading=true;changed_.notify_one();}
ArtworkSnapshot ArtworkLoader::Snapshot(){std::lock_guard<std::mutex>lock(mutex_);return snapshot_;}
void ArtworkLoader::Run(){
    for(;;){Track track;bool refresh;uint64_t version;
        {std::unique_lock<std::mutex>lock(mutex_);changed_.wait(lock,[this]{return pending_||!running_;});if(!running_)return;track=requested_;refresh=refresh_;pending_=false;version=generation_.load();}
        ArtworkSnapshot value;value.trackId=track.id;value.trackPath=track.path;
        auto publish=[&](bool loading){
            if(Cancelled(&generation_,version))return;
            value.loading=loading;value.enrichedTrack=std::make_shared<const Track>(track);
            std::lock_guard<std::mutex>lock(mutex_);
            if(running_&&generation_.load()==version&&!pending_)snapshot_=value;
        };
        RestoreDerived(track,cache_);
        if(track.source==SourceKind::Cloud)cache_.ResolveCloud(track,&generation_,version,refresh);
        if(!track.cover.empty()){auto bytes=cache_.ReadAsset(track.cover,4*1024*1024,&generation_,version,refresh);if(!Cancelled(&generation_,version))value.cover=DecodeCover(bytes);if(!value.cover)value.note="封面暂不可用";}
        publish(true);
        if(!Cancelled(&generation_,version)&&!track.lyrics.empty()){auto bytes=cache_.ReadAsset(track.lyrics,1024*1024,&generation_,version,refresh);if(!bytes.empty())value.lyrics=std::make_shared<Lyrics>(ParseLyrics(std::string(bytes.begin(),bytes.end())));}
        if(!Cancelled(&generation_,version)&&(!value.lyrics || (value.lyrics->lines.empty()&&value.lyrics->plain.empty()))){
            cache_.ResolveLyrics(track,lyricsService_,&generation_,version,refresh);
            if(!track.lyrics.empty()){auto bytes=cache_.ReadAsset(track.lyrics,1024*1024,&generation_,version);if(!bytes.empty())value.lyrics=std::make_shared<Lyrics>(ParseLyrics(std::string(bytes.begin(),bytes.end())));}
        }
        if(!value.lyrics || (value.lyrics->lines.empty()&&value.lyrics->plain.empty())){if(!value.note.empty())value.note+=" · ";value.note+="暂无匹配歌词";}
        cache_.Prune({track.cover,track.lyrics});
        publish(false);
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
