#include "runtime.hpp"
#include "log.hpp"
#include "fslock.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace papa {
struct MaintenanceWorker::Impl {
 struct Work {
  bool save=false,saveSources=false,cache=false,clear=false,persistLimit=false;
  uint64_t serial=0;
  Settings settings;
  MediaResume resume;
  std::unique_ptr<MediaState>full;
  std::vector<MediaSource>sources;
  std::vector<std::string>pins;
 } pending;
 MediaCache&cache;
 std::string root,settingsPath;
 mutable std::mutex mutex;
 std::condition_variable changed,finished;
 MaintenanceSnapshot status;
 bool stopping=false;
#ifdef __SWITCH__
 Thread thread{};bool threadReady=false;
#else
 std::thread thread;
#endif
 Impl(MediaCache&value,std::string directory,std::string path)
  :cache(value),root(std::move(directory)),settingsPath(std::move(path)){
  status.running=true;
#ifdef __SWITCH__
  Result rc=threadCreate(&thread,[](void*arg){static_cast<Impl*>(arg)->Run();},this,nullptr,0x10000,0x31,-2);
  if(R_SUCCEEDED(rc)){rc=threadStart(&thread);if(R_FAILED(rc))threadClose(&thread);}
  threadReady=R_SUCCEEDED(rc);
  if(!threadReady){char error[96];std::snprintf(error,sizeof(error),"后台维护线程启动失败 (0x%08x)",unsigned(rc));status.running=false;status.error=error;}
#else
  try{thread=std::thread([this]{Run();});}
  catch(const std::system_error&){status.running=false;status.error="后台维护线程启动失败";}
#endif
 }
 struct Outcome {std::string save,sources,cache;};
 Outcome Save(Work&work){
  Outcome outcome;std::string part;
  auto fail=[](std::string&error,const std::string&message){if(!error.empty())error+="；";error+=message;};
  if(work.save){
   auto&error=outcome.save;
   if(!CreateDirectories(root))fail(error,"无法创建配置目录");
   if(work.full){work.full->resume=work.resume;
    if(!SaveMediaState(root+"/state.json",*work.full,&part))fail(error,"曲库状态保存失败："+part);}
   MediaState resume;resume.resume=std::move(work.resume);
   if(!SaveMediaState(root+"/resume.json",resume,&part))fail(error,"播放进度保存失败："+part);
   if(!SaveSettings(settingsPath,work.settings))fail(error,"设置保存失败，请检查 SD 卡");
  }
  if(work.saveSources){
   if(!CreateDirectories(root)||!SaveSources(root+"/sources.json",work.sources,&part))
    outcome.sources="来源保存失败："+part;
  }
  if(work.cache){
   cache.Warm();cache.SetPins(work.pins);
   if(work.clear)cache.ClearIdle(work.pins);else cache.Prune(work.pins);
   if(work.persistLimit){
    // Match the existing replacement-safe rename policy while retaining the
    // original decimal-only cache-limit.txt format.
    CreateDirectories(root);
    const auto path=root+"/cache-limit.txt",temporary=path+".tmp",backup=path+".bak";
    FILE*f=OpenUtf8File(temporary,"wb");bool ok=false;
    if(f){ok=std::fprintf(f,"%llu\n",static_cast<unsigned long long>(cache.Limit()))>0;ok=CloseLocked(f)==0&&ok;}
    if(ok){RemoveLocked(backup.c_str());bool haveOld=RenameLocked(path.c_str(),backup.c_str())==0;
     ok=RenameLocked(temporary.c_str(),path.c_str())==0;if(!ok&&haveOld)RenameLocked(backup.c_str(),path.c_str());}
    if(!ok){RemoveLocked(temporary.c_str());outcome.cache="缓存上限保存失败，请检查 SD 卡";}
   }
  }
  return outcome;
 }
 void Run(){
  auto nextRefresh=std::chrono::steady_clock::now();
  std::unique_ptr<MediaState>retryFull;
  std::unique_ptr<std::vector<MediaSource>>retrySources;
  Outcome errors;
  for(;;){
   Work work;bool refresh=false;
   {
    std::unique_lock<std::mutex>lock(mutex);
    changed.wait_until(lock,nextRefresh,[&]{return stopping||pending.serial!=0;});
    if(stopping&&!pending.serial)break;
    work=std::move(pending);pending=Work{};
    refresh=std::chrono::steady_clock::now()>=nextRefresh;
    status.busy=true;
   }
   // A failed full save survives a later lightweight resume update. Keep this
   // snapshot exclusively on the worker, without enlarging the queue lock.
   if(work.save&&!work.full)work.full=std::move(retryFull);
   if(work.save&&!work.saveSources&&retrySources){work.sources=std::move(*retrySources);work.saveSources=true;retrySources.reset();}
   auto outcome=Save(work);
   if(work.save){errors.save=std::move(outcome.save);
    if(errors.save.empty())retryFull.reset();else if(work.full)retryFull=std::move(work.full);}
   if(work.saveSources){errors.sources=std::move(outcome.sources);
    if(errors.sources.empty())retrySources.reset();else retrySources=std::make_unique<std::vector<MediaSource>>(std::move(work.sources));}
   if(work.cache)errors.cache=std::move(outcome.cache);
   if(refresh){cache.Warm();cache.RefreshStreams(false);nextRefresh=std::chrono::steady_clock::now()+std::chrono::seconds(1);}
   {
    std::lock_guard<std::mutex>lock(mutex);
    if(work.serial){status.completed=work.serial;status.error=errors.save;
     for(const auto*error:{&errors.sources,&errors.cache})if(!error->empty()){if(!status.error.empty())status.error+="；";status.error+=*error;}}
    status.busy=false;
   }
   finished.notify_all();
  }
  {std::lock_guard<std::mutex>lock(mutex);status.running=false;status.busy=false;}
  finished.notify_all();
 }
};
MaintenanceWorker::MaintenanceWorker(MediaCache&cache,std::string root,std::string path)
 :impl_(new Impl(cache,std::move(root),std::move(path))){}
MaintenanceWorker::~MaintenanceWorker(){Shutdown();}
void MaintenanceWorker::QueueSave(Settings settings,const MediaState&state,bool full){
 // The potentially large full-state copy is a user-change cost, never a
 // periodic resume cost. Retire a superseded snapshot outside the queue lock.
 std::unique_ptr<MediaState>copy;if(full)copy.reset(new MediaState(state));
 MediaResume resume=state.resume;
 {std::lock_guard<std::mutex>lock(impl_->mutex);
  if(impl_->stopping||!impl_->status.running)return;
  auto&work=impl_->pending;work.save=true;work.settings=settings;work.resume=std::move(resume);
  if(full)copy.swap(work.full);
  work.serial=++impl_->status.queued;
 }
 impl_->changed.notify_one();
}
void MaintenanceWorker::QueueCacheMaintenance(std::vector<std::string>pins,bool clear,bool persistLimit){
 {std::lock_guard<std::mutex>lock(impl_->mutex);
  if(impl_->stopping||!impl_->status.running)return;
  auto&work=impl_->pending;work.cache=true;work.pins.swap(pins);
  work.clear=work.clear||clear;work.persistLimit=work.persistLimit||persistLimit;
  work.serial=++impl_->status.queued;
 }
 impl_->changed.notify_one();
}
void MaintenanceWorker::QueueSources(std::vector<MediaSource>sources){
 {std::lock_guard<std::mutex>lock(impl_->mutex);
  if(impl_->stopping||!impl_->status.running)return;
  auto&work=impl_->pending;work.saveSources=true;work.sources.swap(sources);
  work.serial=++impl_->status.queued;
 }
 impl_->changed.notify_one();
}
MaintenanceSnapshot MaintenanceWorker::Snapshot()const{std::lock_guard<std::mutex>lock(impl_->mutex);return impl_->status;}
bool MaintenanceWorker::Flush(){
 std::unique_lock<std::mutex>lock(impl_->mutex);const auto target=impl_->status.queued;
 impl_->finished.wait(lock,[&]{return impl_->status.completed>=target||!impl_->status.running;});
 return impl_->status.completed>=target&&impl_->status.error.empty();
}
void MaintenanceWorker::Shutdown(){
 {std::lock_guard<std::mutex>lock(impl_->mutex);impl_->stopping=true;}
 impl_->changed.notify_one();
#ifdef __SWITCH__
 if(impl_->threadReady){threadWaitForExit(&impl_->thread);threadClose(&impl_->thread);impl_->threadReady=false;}
#else
 if(impl_->thread.joinable())impl_->thread.join();
#endif
}
std::string CatalogCachePath(const MediaCache&c,const MediaSource&s){uint64_t h=14695981039346656037ull;for(unsigned char v:s.id+"\n"+s.url){h^=v;h*=1099511628211ull;}char key[40];std::snprintf(key,sizeof(key),"/%016llx.catalog.json",static_cast<unsigned long long>(h));return c.MetadataDirectory()+key;}
std::vector<Track>ScanWithMetadata(const std::string&root,const std::string&index,MediaCache&cache,
 const std::map<std::string,MediaOverride>&overrides,const std::atomic<uint64_t>&cancel,uint64_t generation,std::string&error,const std::vector<std::string>&pinned){
 cache.Warm();
 std::vector<Track>old;LoadLibraryIndex(index,old);std::map<std::string,const Track*>byPath;for(const auto&t:old)byPath[t.path]=&t;
 auto tracks=ScanLibrary(root);std::vector<Track>raw;raw.reserve(tracks.size());
 for(auto&t:tracks){if(cancel.load()!=generation)return {};struct stat st{};if(stat(t.path.c_str(),&st))continue;
  uint64_t size=static_cast<uint64_t>(st.st_size),mtime=static_cast<uint64_t>(st.st_mtime);auto found=byPath.find(t.path);
  if(found!=byPath.end()&&found->second->fileSize==size&&found->second->modified==mtime)t=*found->second;
  else cache.Enrich(t);
  t.fileSize=size;t.modified=mtime;raw.push_back(t);cache.Prune(pinned);
 }
 if(cancel.load()!=generation)return {};
 SaveLibraryIndex(index,raw,&error);
 for(auto&t:raw){auto o=overrides.find(t.id);if(o!=overrides.end())ApplyOverride(t,o->second);}return raw;
}
std::string PreferredCatalogUrl(const std::string&url){
 // Preserve the configured source identity/cache key and existing favorites.
 // This server's JSON carries stable URL-hash IDs plus covers and LRC URLs.
 if(url=="https://kiro.papaclaoud.top/library.m3u")return "https://kiro.papaclaoud.top/library.json";
 return url;
}
LibraryLoadResult LoadLibraries(const std::string&root,const std::string&config,MediaCache&cache,const std::vector<MediaSource>&sources,
 const std::atomic<uint64_t>&cancel,uint64_t generation,const std::vector<std::string>&pinned){
 LibraryLoadResult out;std::map<std::string,MediaOverride>overrides;std::string error;LoadOverrides(config+"/overrides.json",overrides,&error);
 auto pins=pinned;for(const auto&s:sources)if(s.enabled){auto path=CatalogCachePath(cache,s);pins.push_back(path);pins.push_back(path+".bak");}
 cache.SetPins(pins);cache.Warm();
 out.sd=ScanWithMetadata(root,config+"/sd-library.json",cache,overrides,cancel,generation,out.notice,pins);
 Logf("SD 扫描完成 %zu 首",out.sd.size());
 for(const auto&s:sources){if(!s.enabled)continue;if(cancel.load()!=generation)return out;
  Logf("拉取来源 %s：%s",s.id.c_str(),s.url.c_str());
  auto preferred=PreferredCatalogUrl(s.url);
  auto result=FetchUrl(preferred,8*1024*1024,&cancel,generation);std::vector<Track>tracks;
  error=result.error;if(error.empty()&&!result.cancelled)tracks=ParseCatalog(std::string(result.bytes.begin(),result.bytes.end()),result.finalUrl,s.id,error);
  if(!error.empty()&&!result.cancelled&&preferred!=s.url&&cancel.load()==generation){
   result=FetchUrl(s.url,8*1024*1024,&cancel,generation);error=result.error;
   if(error.empty()&&!result.cancelled)tracks=ParseCatalog(std::string(result.bytes.begin(),result.bytes.end()),result.finalUrl,s.id,error);
  }
  Logf("来源 %s 返回 %zu 字节，解析出 %zu 首，错误=%s",s.id.c_str(),result.bytes.size(),tracks.size(),error.c_str());
  if(result.cancelled||cancel.load()!=generation)return out;
  auto path=CatalogCachePath(cache,s);
  if(!error.empty()){out.stale=true;out.notice=s.name+": "+error+"（保留上次曲库）";tracks.clear();LoadLibraryIndex(path,tracks);}
  else {std::string saveError;
   if(!cache.Reserve(16ull*1024*1024,pins))out.notice="已更新但离线清单未保存，缓存空间不足";
   else {if(!SaveLibraryIndex(path,tracks,&saveError))out.notice=saveError;cache.RecordAsset(path);cache.RecordAsset(path+".bak");}
  }
  for(auto&t:tracks){if(out.sd.size()+out.cloud.size()>=10000){out.notice="曲库已达 10000 首上限";break;}
   auto o=overrides.find(t.id);if(o!=overrides.end())ApplyOverride(t,o->second);out.cloud.push_back(std::move(t));}
  cache.Prune(pins);
 }
 return out;
}
}
