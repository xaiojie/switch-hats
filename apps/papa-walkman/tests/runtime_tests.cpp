#include "runtime.hpp"
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
#include "fslock.hpp"
#include "log.hpp"
#ifndef _WIN32
#include <unistd.h>
#endif
#include <chrono>
#include <filesystem>
#include <future>
#include <thread>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::printf("FAIL runtime %d: %s\n",__LINE__,#x);return 1;}}while(0)
template<class Predicate> bool WaitFor(Predicate ready){
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!ready()){if(std::chrono::steady_clock::now()>=end)return false;std::this_thread::yield();}
 return true;
}
int MaintenanceTests(){
 // A slow SD write must not block queueing or status reads, and a periodic
 // resume-only request must not erase already queued favorites/playlists.
 const std::string root="build-host/runtime-maintenance";
 std::filesystem::remove_all(root);CHECK(CreateDirectories(root));
 MediaCache cache(root+"/cache");cache.Warm();
 Settings settings;settings.volume=41;settings.mode=Mode::Shuffle;settings.theme=Theme::NightRecord;
 MediaState full;full.favorites.insert("cloud:test:first");
 full.resume.valid=true;full.resume.track={"https://example.invalid/song.mp3","Song","Artist"};
 full.resume.track.id="cloud:test:song";full.resume.track.source=SourceKind::Cloud;full.resume.position=1;
 MaintenanceWorker worker(cache,root,root+"/settings.ini");
 CHECK(worker.Snapshot().running);
 {
  FsGuard holdDisk;
  worker.QueueSave(settings,full,true);
  CHECK(WaitFor([&]{return worker.Snapshot().busy;}));
  auto start=std::chrono::steady_clock::now();
  full.favorites={"cloud:test:keep"};full.playlists.push_back({"saved","Saved",{full.resume.track}});
  worker.QueueSave(settings,full,true);
  full.favorites.insert("cloud:test:latest");worker.QueueSave(settings,full,true);
  MediaState resume;resume.resume=full.resume;resume.resume.position=99;settings.volume=63;settings.theme=Theme::LyricTheatre;
  worker.QueueSave(settings,resume,false);
  worker.QueueSources({{"old","Old","https://example.invalid/old.json"}});
  worker.QueueSources({{"latest","Latest","https://example.invalid/latest.json"}});
  auto snapshot=worker.Snapshot();CHECK(snapshot.queued>=4);CHECK(snapshot.busy);
  CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
 }
 CHECK(worker.Flush());CHECK(worker.Snapshot().error.empty());
 MediaState saved,resume;CHECK(LoadMediaState(root+"/state.json",saved));CHECK(LoadMediaState(root+"/resume.json",resume));
 CHECK(saved.favorites==std::set<std::string>({"cloud:test:keep","cloud:test:latest"}));
 CHECK(saved.playlists.size()==1);CHECK(saved.resume.position==99);CHECK(resume.resume.position==99);
 CHECK(resume.favorites.empty());CHECK(LoadSettings(root+"/settings.ini").volume==63);
 CHECK(LoadSettings(root+"/settings.ini").theme==Theme::LyricTheatre);
 std::vector<MediaSource> sources;CHECK(LoadSources(root+"/sources.json",sources));
 CHECK(sources.size()==1);CHECK(sources[0].id=="latest");
 // Shutdown has to wait for the outstanding write, join, and persist the latest
 // request before the caller can destroy its cache or close SD services.
 std::future<void> stopped;
 {
  FsGuard holdDisk;
  full.resume.position=117;worker.QueueSave(settings,full,true);
  CHECK(WaitFor([&]{return worker.Snapshot().busy;}));
  stopped=std::async(std::launch::async,[&]{worker.Shutdown();});
  CHECK(stopped.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
 }
 CHECK(stopped.wait_for(std::chrono::seconds(3))==std::future_status::ready);stopped.get();
 CHECK(!worker.Snapshot().running);CHECK(LoadMediaState(root+"/state.json",saved));CHECK(saved.resume.position==117);
 // Destruction itself is a final flush, without requiring a separate Flush call.
 {MaintenanceWorker final(cache,root,root+"/settings.ini");full.resume.position=150;final.QueueSave(settings,full,true);}
 CHECK(LoadMediaState(root+"/resume.json",resume));CHECK(resume.resume.position==150);
 // Errors are observable without disk access, and a subsequent successful save
 // clears the stale error rather than permanently reporting a failed state.
 const auto bad=root+"/blocked";{std::ofstream f(bad);f<<"file blocks directory";}
 {MaintenanceWorker failing(cache,bad,bad+"/settings.ini");failing.QueueSave(settings,full,true);
  failing.QueueSources({{"retry","Retry","https://example.invalid/retry.json"}});
  CHECK(!failing.Flush());CHECK(!failing.Snapshot().error.empty());
  failing.QueueCacheMaintenance({});CHECK(!failing.Flush());CHECK(!failing.Snapshot().error.empty());
  CHECK(std::remove(bad.c_str())==0);CHECK(CreateDirectories(bad));
  MediaState update;update.resume=full.resume;update.resume.position=201;
  failing.QueueSave(settings,update,false);CHECK(failing.Flush());CHECK(failing.Snapshot().error.empty());
  CHECK(LoadMediaState(bad+"/state.json",saved));CHECK(saved.favorites==full.favorites);CHECK(saved.resume.position==201);
  CHECK(LoadSources(bad+"/sources.json",sources));CHECK(sources.size()==1);CHECK(sources[0].id=="retry");}
 // Cache work is also queued; active streams and explicitly pinned assets stay
 // accounted for while idle metadata can be removed without blocking the UI.
 {const auto keep=cache.MetadataDirectory()+"/1111111111111111.lrc",idle=cache.MetadataDirectory()+"/2222222222222222.lrc";
  {std::ofstream f(keep);f<<"keep";}{std::ofstream f(idle);f<<"idle";}cache.RecordAsset(keep);cache.RecordAsset(idle);
  MaintenanceWorker cleaner(cache,root,root+"/settings.ini");cache.SetLimit(4096);
  cleaner.QueueCacheMaintenance({keep},true,true);CHECK(cleaner.Flush());
  CHECK(std::filesystem::exists(keep));CHECK(!std::filesystem::exists(idle));
  std::ifstream limit(root+"/cache-limit.txt");uint64_t value=0;limit>>value;CHECK(value==4096);
  const auto active=cache.StreamDirectory()+"/papa-stream-maintenance.tmp";{std::ofstream f(active);f<<"stream";}
  CHECK(WaitFor([&]{return cache.Usage().files==2&&cache.Usage().bytes==10;}));
  cleaner.Shutdown();CHECK(std::filesystem::exists(active));}
 std::filesystem::remove_all(root);
 return 0;
}
int LoggerTests(){
#ifndef _WIN32
 // Slow output is exercised through a real pipe: its reader waits before
 // draining. Logging must enqueue/drop promptly while its owned writer waits.
 int output[2];CHECK(pipe(output)==0);int original=dup(STDERR_FILENO);CHECK(original>=0);
 CHECK(dup2(output[1],STDERR_FILENO)>=0);close(output[1]);
 std::string captured;
 std::thread reader([&]{std::this_thread::sleep_for(std::chrono::milliseconds(300));char chunk[4096];ssize_t n;
  while((n=read(output[0],chunk,sizeof(chunk)))>0)captured.append(chunk,size_t(n));close(output[0]);});
 LogInit();LogStartWatchdog();
 const std::string message(950,'x');auto start=std::chrono::steady_clock::now();
 for(int i=0;i<4096;++i)Logf("bounded logger %d %s",i,message.c_str());
 const bool responsive=std::chrono::steady_clock::now()-start<std::chrono::milliseconds(150);
 auto closed=std::async(std::launch::async,[]{LogClose();});
 const bool waited=closed.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
 closed.wait();closed.get();
 CHECK(dup2(original,STDERR_FILENO)>=0);close(original);reader.join();
 CHECK(responsive);CHECK(waited);CHECK(captured.find("bounded logger")!=std::string::npos);
 CHECK(captured.size()<512*1024); // Saturation is bounded, not an unbounded queue.
#endif
 return 0;
}
int main(){
 CHECK(MaintenanceTests()==0);CHECK(LoggerTests()==0);
 std::string root="build-host/runtime-fixture";CHECK(CreateDirectories(root+"/music"));
 auto path=root+"/music/tone.mp3",index=root+"/index.json";
 {std::ofstream f(path,std::ios::binary);f<<"synthetic audio fixture";}
 struct stat st{};CHECK(!stat(path.c_str(),&st));
 Track track{path,"cached base title","cached artist"};track.id="sd:cached";track.fileSize=st.st_size;track.modified=st.st_mtime;
 CHECK(SaveLibraryIndex(index,{track}));MediaCache cache(root+"/cache");
 std::map<std::string,MediaOverride>overrides;overrides[track.id].title="user override";std::atomic<uint64_t>cancel{1};std::string error;
 auto result=ScanWithMetadata(root+"/music",index,cache,overrides,cancel,1,error);
 CHECK(error.empty());CHECK(result.size()==1);CHECK(result[0].title=="user override");CHECK(result[0].artist=="cached artist");
 std::vector<Track>raw;CHECK(LoadLibraryIndex(index,raw));CHECK(raw.size()==1);CHECK(raw[0].title=="cached base title");
 {std::ofstream f(path,std::ios::binary|std::ios::app);f<<" size changed";}
 result=ScanWithMetadata(root+"/music",index,cache,{},cancel,1,error);
 CHECK(result.size()==1);CHECK(result[0].title!="cached base title");CHECK(result[0].fileSize>track.fileSize);
 cancel=2;CHECK(ScanWithMetadata(root+"/music",index,cache,{},cancel,1,error).empty());
 MediaSource a{"same","source","https://a.invalid/list.json"},b=a;b.url="https://b.invalid/list.json";
 CHECK(PreferredCatalogUrl("https://kiro.papaclaoud.top/library.m3u")=="https://kiro.papaclaoud.top/library.json");
 CHECK(PreferredCatalogUrl("https://a.invalid/library.m3u")=="https://a.invalid/library.m3u");
 CHECK(PreferredCatalogUrl("https://kiro.papaclaoud.top/library.json")=="https://kiro.papaclaoud.top/library.json");
 CHECK(CatalogCachePath(cache,a)!=CatalogCachePath(cache,b));
 std::remove(path.c_str());std::remove(index.c_str());std::remove((index+".bak").c_str());cache.ClearIdle();
 std::puts("PASS runtime: background coalesced persistence, retry, shutdown join, queued sources/cache, bounded asynchronous logging, metadata reuse and cancellation");
}
