#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "stream.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sys/stat.h>
#include <limits>
#include <thread>
#include <curl/curl.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace papa {
FILE* OpenUtf8File(const std::string& path,const char* mode){
#ifdef _WIN32
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);if(!n)return nullptr;
 std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&w[0],n);
 std::wstring m;for(;*mode;++mode)m+=wchar_t(*mode);return _wfopen(w.c_str(),m.c_str());
#else
 return std::fopen(path.c_str(),mode);
#endif
}
static void removeUtf8File(const std::string& path){
#ifdef _WIN32
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);if(!n)return;std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.c_str(),-1,&w[0],n);_wremove(w.c_str());
#else
 std::remove(path.c_str());
#endif
}
static bool seekFile(FILE* f,uint64_t n){
#ifdef _WIN32
 return _fseeki64(f,static_cast<int64_t>(n),SEEK_SET)==0;
#else
 return fseeko(f,static_cast<off_t>(n),SEEK_SET)==0;
#endif
}
struct Request {
 std::mutex mutex,cancelMutex;std::condition_variable cv;std::atomic<bool> stop{false};
 const std::atomic<uint64_t>* cancel=nullptr;uint64_t expected=0;
 std::shared_ptr<StreamStatus> status;StreamSnapshot snapshot;StreamOptions options;
 FILE* cache=nullptr;uint64_t pos=0,end=0,start=0,offset=0,body=0,length=0,rangeStart=0,rangeEnd=0,rangeTotal=0,previousTotal=0;
 bool done=false,headers=false,hasLength=false,hasRange=false;long code=0;
 std::string url,etag,previousEtag,cachePath;CURL* easy=nullptr;
 ~Request(){if(cache)std::fclose(cache);if(!cachePath.empty())removeUtf8File(cachePath);}
 bool createCache(){
  std::string dir=options.cacheDirectory;
#ifdef _WIN32
  if(dir.empty()){wchar_t path[MAX_PATH]={};if(!GetTempPathW(MAX_PATH,path))return false;int n=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);if(n<=1)return false;dir.resize(n);WideCharToMultiByte(CP_UTF8,0,path,-1,&dir[0],n,nullptr,nullptr);dir.pop_back();}
#else
  if(dir.empty()){mkdir("sdmc:/config",0777);mkdir("sdmc:/config/papa-walkman",0777);dir="sdmc:/config/papa-walkman/cache";mkdir(dir.c_str(),0777);}
#endif
  while(!dir.empty()&&(dir.back()=='/'||dir.back()=='\\'))dir.pop_back();
  static std::atomic<uint64_t> id{0};
  cachePath=dir+"/papa-stream-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++id)+".tmp";
  cache=OpenUtf8File(cachePath,"w+b");return cache!=nullptr;
 }
 // Lock order is mutex -> cancelMutex. progress() takes only cancelMutex.
 bool cancelled(){std::lock_guard<std::mutex> lock(cancelMutex);return stop.load()||(cancel&&cancel->load()!=expected);}
 void RetireCancellation(){std::lock_guard<std::mutex> lock(cancelMutex);stop=true;cancel=nullptr;}
 void publish(){if(stop.load())return;snapshot.cachedStart=start;snapshot.cachedEnd=end;snapshot.seekable=end>start;status->Update(snapshot);}
 void fail(const std::string& e){snapshot.error=e;done=true;snapshot.buffering=false;publish();cv.notify_all();}
 static int progress(void* u,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Request*>(u)->cancelled()?1:0;}
 static size_t header(char* p,size_t a,size_t b,void* u){
  auto& r=*static_cast<Request*>(u);size_t n=a*b;std::string s(p,n);std::lock_guard<std::mutex> lock(r.mutex);
  if(s.compare(0,5,"HTTP/")==0){r.code=0;auto i=s.find(' ');if(i!=std::string::npos)r.code=std::strtol(s.c_str()+i+1,nullptr,10);r.hasLength=r.hasRange=false;r.length=0;r.etag.clear();r.headers=false;}
  auto colon=s.find(':');if(colon!=std::string::npos){std::string key=s.substr(0,colon);for(char&c:key)c=char(std::tolower(static_cast<unsigned char>(c)));std::string v=s.substr(colon+1);while(!v.empty()&&(v.back()=='\r'||v.back()=='\n'))v.pop_back();while(!v.empty()&&v.front()==' ')v.erase(0,1);
   if(key=="content-length"){char* e=nullptr;r.length=std::strtoull(v.c_str(),&e,10);r.hasLength=e&&!*e;}
   if(key=="etag")r.etag=v;
   if(key=="content-range"){unsigned long long x=0,y=0,z=0;r.hasRange=std::sscanf(v.c_str(),"bytes %llu-%llu/%llu",&x,&y,&z)==3;r.rangeStart=x;r.rangeEnd=y;r.rangeTotal=z;}
  }
  if(s=="\r\n"&&r.code>=200&&r.code<300){
   if(r.code!=200&&r.code!=206){r.fail("Unsupported HTTP response "+std::to_string(r.code));return 0;}
   if(r.code==206&&(!r.hasRange||r.rangeStart!=r.offset||r.rangeEnd<r.rangeStart||r.rangeEnd>=r.rangeTotal||r.rangeEnd+1!=r.rangeTotal||(r.hasLength&&r.length!=r.rangeEnd-r.rangeStart+1))){r.fail("Invalid HTTP Content-Range; retry track");return 0;}
   if((!r.previousEtag.empty()&&r.etag!=r.previousEtag)||(r.previousTotal&&(r.code==206?r.rangeTotal:(r.hasLength?r.length:0))!=r.previousTotal)){r.fail("Remote file changed; reopen track");return 0;}
   char* effective=nullptr;if(r.easy)curl_easy_getinfo(r.easy,CURLINFO_EFFECTIVE_URL,&effective);if(effective)r.snapshot.finalUrl=effective;
   r.snapshot.rangeSupported=r.code==206;r.end=r.start=r.code==206?r.offset:0;
   r.snapshot.totalBytes=r.code==206?r.rangeTotal:(r.hasLength?r.length:0);r.headers=true;r.publish();
  }return n;
 }
 static size_t write(char* data,size_t a,size_t b,void* u){
  auto& r=*static_cast<Request*>(u);size_t count=a*b;
  if(r.code>=300&&r.code<400)return count;
  std::unique_lock<std::mutex> lock(r.mutex);if(!r.headers)return 0;
  size_t used=0;
  while(used<count){
   while(r.end>=r.pos+r.options.prefetchBytes&&!r.cancelled())r.cv.wait_for(lock,std::chrono::milliseconds(25));
   if(r.cancelled())return 0;
   size_t n=static_cast<size_t>(std::min<uint64_t>(count-used,r.options.cacheBytes-r.end%r.options.cacheBytes));
   n=std::min(n,r.options.prefetchBytes);
   if(!seekFile(r.cache,r.end%r.options.cacheBytes)||std::fwrite(data+used,1,n,r.cache)!=n){r.fail("Stream cache write failed; check free space");return 0;}
   r.end+=n;r.body+=n;used+=n;r.start=std::max(r.start,r.end>r.options.cacheBytes?r.end-r.options.cacheBytes:0);
   r.snapshot.downloadedBytes=r.end;r.snapshot.buffering=false;r.publish();r.cv.notify_all();
  }return count;
 }
 void run(){
  static const bool initialized=[](){return curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;}();
  CURL* c=initialized?curl_easy_init():nullptr;
  if(!c){std::lock_guard<std::mutex> lock(mutex);fail("HTTP initialization failed");return;}
  easy=c;char err[CURL_ERROR_SIZE]={};std::string range=std::to_string(offset)+"-";
  curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_RANGE,range.c_str());
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
  curl_easy_setopt(c,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));
  curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,30L);
  curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_ERRORBUFFER,err);
  curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,header);curl_easy_setopt(c,CURLOPT_HEADERDATA,this);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write);curl_easy_setopt(c,CURLOPT_WRITEDATA,this);
  curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,this);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
  auto result=curl_easy_perform(c);char* finalUrl=nullptr;curl_easy_getinfo(c,CURLINFO_EFFECTIVE_URL,&finalUrl);
  {std::lock_guard<std::mutex> lock(mutex);if(finalUrl)snapshot.finalUrl=finalUrl;
   snapshot.cancelled=cancelled();snapshot.buffering=false;done=true;
   if(snapshot.error.empty()&&!snapshot.cancelled){if(result!=CURLE_OK)snapshot.error="HTTP "+std::to_string(code)+": "+(err[0]?err:curl_easy_strerror(result))+"; retry track";
    else if((hasLength&&body!=length)||(code==206&&body!=rangeEnd-rangeStart+1))snapshot.error="Incomplete HTTP file; retry track";else snapshot.eof=true;}
   publish();cv.notify_all();}
  curl_easy_cleanup(c);
 }
};
struct ByteSource::Impl {FILE* file=nullptr;std::shared_ptr<Request> request;std::shared_ptr<StreamStatus> status;StreamSnapshot local;const std::atomic<uint64_t>* cancel=nullptr;uint64_t expected=0;};
ByteSource::ByteSource():p_(new Impl){}ByteSource::~ByteSource(){Close();}
void ByteSource::Close(){if(p_->request){{std::lock_guard<std::mutex> lock(p_->request->mutex);p_->request->RetireCancellation();p_->request->cv.notify_all();}p_->request.reset();}if(p_->file)std::fclose(p_->file);p_->file=nullptr;p_->cancel=nullptr;}
bool ByteSource::Open(const std::string& path,const std::atomic<uint64_t>* cancel,uint64_t expected,std::shared_ptr<StreamStatus> status,StreamOptions opts){
 Close();p_->status=status?status:std::make_shared<StreamStatus>();p_->local={};p_->cancel=cancel;p_->expected=expected;p_->status->Update({});
 if(cancel&&cancel->load()!=expected){p_->local.cancelled=true;p_->status->Update(p_->local);return false;}
 if(path.compare(0,7,"http://")&&path.compare(0,8,"https://")){p_->file=OpenUtf8File(path,"rb");if(!p_->file){p_->local.error="Cannot read file";p_->status->Update(p_->local);return false;}p_->local.seekable=true;p_->status->Update(p_->local);return true;}
 auto r=std::make_shared<Request>();r->url=path;r->cancel=cancel;r->expected=expected;r->status=p_->status;r->options=opts;
 r->options.cacheBytes=std::min<uint64_t>(512ull*1024*1024,std::max<uint64_t>(32768,opts.cacheBytes));r->options.prefetchBytes=static_cast<size_t>(std::min<uint64_t>(r->options.cacheBytes/2,std::max<size_t>(16384,opts.prefetchBytes)));
 if(!r->createCache()){p_->local.error="Cannot create stream cache";p_->status->Update(p_->local);return false;}
 r->snapshot.buffering=true;r->publish();p_->request=r;std::thread([r](){r->run();}).detach();return true;
}
size_t ByteSource::Read(void* out,size_t count){
 if(!count)return 0;
 if(!p_->request&&p_->cancel&&p_->cancel->load()!=p_->expected){p_->local.cancelled=true;p_->status->Update(p_->local);return 0;}
 if(p_->file){auto n=std::fread(out,1,count,p_->file);p_->local.eof=std::feof(p_->file)!=0;if(std::ferror(p_->file))p_->local.error="File read failed";p_->status->Update(p_->local);return n;}
 auto r=p_->request;if(!r)return 0;std::unique_lock<std::mutex> lock(r->mutex);
 while(r->pos>=r->end&&!r->done&&!r->cancelled()){r->snapshot.buffering=true;r->publish();r->cv.wait_for(lock,std::chrono::milliseconds(25));}
 if(r->cancelled()){r->snapshot.cancelled=true;r->snapshot.buffering=false;r->publish();return 0;}
 if(r->pos<r->start){r->fail("Seek outside retained stream cache");return 0;}
 if(r->pos>=r->end)return 0;
 size_t n=static_cast<size_t>(std::min<uint64_t>(count,r->end-r->pos));n=std::min<uint64_t>(n,r->options.cacheBytes-r->pos%r->options.cacheBytes);
 if(n&&(!seekFile(r->cache,r->pos%r->options.cacheBytes)||std::fread(out,1,n,r->cache)!=n)){r->fail("Stream cache read failed");return 0;}
 r->pos+=n;r->snapshot.buffering=false;r->publish();r->cv.notify_all();return n;
}
bool ByteSource::Seek(uint64_t offset){
 if(p_->file)return seekFile(p_->file,offset);
 auto old=p_->request;if(!old)return false;std::unique_lock<std::mutex> lock(old->mutex);
 if(old->cancelled())return false;
 if(old->snapshot.totalBytes&&offset>old->snapshot.totalBytes)return false;
 // Exact EOF needs no invalid Range request; finish the current transport instead.
 if(old->snapshot.totalBytes&&offset==old->snapshot.totalBytes){old->pos=offset;old->cv.notify_all();return true;}
 if(offset>=old->start&&offset<=old->end){old->pos=offset;old->cv.notify_all();return true;}
 if(!old->snapshot.rangeSupported)return false;
 auto r=std::make_shared<Request>();r->url=old->url;{std::lock_guard<std::mutex> cancelLock(old->cancelMutex);r->cancel=old->cancel;}r->expected=old->expected;r->status=old->status;r->options=old->options;r->previousEtag=old->etag;r->previousTotal=old->snapshot.totalBytes;
 r->offset=r->pos=r->start=r->end=offset;if(!r->createCache())return false;
 old->RetireCancellation();old->cv.notify_all();r->snapshot.buffering=true;r->publish();p_->request=r;lock.unlock();std::thread([r](){r->run();}).detach();return true;
}
bool ByteSource::Finish(){
 unsigned char bytes[8192];while(Read(bytes,sizeof bytes)){}
 auto status=Status();return status.eof&&status.error.empty()&&!status.cancelled;
}
StreamSnapshot ByteSource::Status()const{return p_->status?p_->status->Snapshot():StreamSnapshot{};}
bool ByteSource::IsRemote()const{return bool(p_->request);}
FetchResult FetchUrl(const std::string& url,size_t maximum,const std::atomic<uint64_t>* cancel,uint64_t expected){
 FetchResult result;ByteSource source;StreamOptions opts;opts.cacheBytes=std::max<size_t>(32768,std::min<size_t>(maximum,1024*1024));
 if(url.compare(0,7,"http://")&&url.compare(0,8,"https://")){result.error="Expected HTTP/HTTPS URL";return result;}
 if(!source.Open(url,cancel,expected,{},opts)){result.error=source.Status().error;result.cancelled=source.Status().cancelled;return result;}
 uint8_t block[8192];size_t n;
 while((n=source.Read(block,sizeof block))){if(n>maximum-result.bytes.size()){result.error="HTTP asset exceeds size limit";result.finalUrl=source.Status().finalUrl;source.Close();result.bytes.clear();return result;}result.bytes.insert(result.bytes.end(),block,block+n);}
 auto s=source.Status();result.finalUrl=s.finalUrl;result.error=s.error;result.cancelled=s.cancelled;if(result.cancelled)result.error="Request cancelled";if(!result.error.empty())result.bytes.clear();return result;
}
}
