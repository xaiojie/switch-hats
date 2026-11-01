#include <switch.h>
#include "audio.hpp"
#include "log.hpp"
#include "fslock.hpp"
#include "ui.hpp"
#include "runtime.hpp"
#include "interaction.hpp"
#include "lyric_motion.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <thread>
#include <array>
#include <map>

using namespace papa;
namespace {
constexpr char MusicRoot[]="sdmc:/music";
constexpr char Config[]="sdmc:/config/papa-walkman/settings.ini";
void WaitExit(const char*message){
    consoleInit(nullptr);std::puts(message);PadState pad{};padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;consoleUpdate(nullptr);}
    consoleExit(nullptr);
}
}
namespace {
const std::string ConfigRoot="sdmc:/config/papa-walkman";
// 本工程编译带 -fno-exceptions：std::thread 创建失败时 libstdc++ 内部抛
// std::system_error，我们接不住 → terminate -> abort -> _exit，进程静默消失
// 且不留崩溃报告。扫描线程用 libnx threadCreate，失败只返回 Result。
// sources/keep 必须是任务自己的拷贝：它们是 startScan 里的局部变量，
// 线程启动后 startScan 就返回了，传指针会读已析构的内存（0.3.10 就是这么
// 把来源名字读成乱码、然后在扫描线程里抛异常、静默退出的）。
struct ScanTask{LibraryLoadResult*pending;std::vector<MediaSource>sources;MediaCache*cache;std::vector<std::string>keep;std::atomic<uint64_t>*generation;uint64_t wanted;std::atomic<bool>*done;bool reloadSources=false;};
void ScanTaskEntry(void*arg){auto*t=static_cast<ScanTask*>(arg);
 if(t->reloadSources)LoadSources(ConfigRoot+"/sources.json",t->sources);
 *t->pending=LoadLibraries(MusicRoot,ConfigRoot,*t->cache,t->sources,*t->generation,t->wanted,t->keep);
 t->pending->sources=t->sources;
 t->done->store(true,std::memory_order_release);delete t;}
// libnx 的 framebufferBegin 在 dequeue 失败时会直接 panic（0x2b59）。
// 这里复制同样的取帧逻辑，但把失败变成"跳过这一帧"，避免整个 App 崩掉。
uint32_t*TryBeginFrame(Framebuffer&fb,u32*stride){
 if(!fb.has_init||!fb.win)return nullptr;
 s32 slot=0;
 if(R_FAILED(nwindowDequeueBuffer(fb.win,&slot,nullptr)))return nullptr;
 if(stride)*stride=fb.stride;
 return fb.buf_linear?static_cast<uint32_t*>(fb.buf_linear):reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(fb.buf)+size_t(slot)*size_t(fb.fb_size));
}
void LogMemory(const char*where){
 u64 used=0,total=0;
 if(R_SUCCEEDED(svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0))&&R_SUCCEEDED(svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0)))
  Logf("%s: 内存 %llu/%llu MB",where,(unsigned long long)(used/1048576ull),(unsigned long long)(total/1048576ull));
}
struct FrameMetrics{
 std::array<u64,600>work{},draw{};size_t count=0;u64 loops=0,renders=0,slow=0,maxWork=0;
 void Add(u64 cpu,u64 paint){++loops;if(paint)++renders;if(cpu>16666667)++slow;maxWork=std::max(maxWork,cpu);if(count<work.size()){work[count]=cpu;draw[count]=paint;++count;}}
 void Report(){if(!count)return;auto cpu=work,paint=draw;size_t at=std::min(count-1,count*95/100);std::nth_element(cpu.begin(),cpu.begin()+at,cpu.begin()+count);std::nth_element(paint.begin(),paint.begin()+at,paint.begin()+count);
  Logf("性能 loops=%llu rendered=%llu cpu_p95=%lluus draw_p95=%lluus cpu_max=%lluus slow=%llu",(unsigned long long)loops,(unsigned long long)renders,(unsigned long long)(cpu[at]/1000),(unsigned long long)(paint[at]/1000),(unsigned long long)(maxWork/1000),(unsigned long long)slow);*this={};}
};
std::string Keyboard(const char*hint,const std::string&initial={}){
 SwkbdConfig keyboard{};if(R_FAILED(swkbdCreate(&keyboard,0)))return {};
 swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetHeaderText(&keyboard,hint);
 swkbdConfigSetInitialText(&keyboard,initial.c_str());swkbdConfigSetStringLenMax(&keyboard,2048);
 char output[8192]{};Result rc=swkbdShow(&keyboard,output,sizeof(output));swkbdClose(&keyboard);
 return R_SUCCEEDED(rc)?std::string(output):std::string{};
}
bool WritePlaylist(const std::string&path,const std::string&text){
 auto temp=path+".tmp",backup=path+".bak";FILE*f=OpenUtf8File(temp,"wb");if(!f)return false;
 bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();ok=papa::CloseLocked(f)==0&&ok;if(!ok){papa::RemoveLocked(temp.c_str());return false;}
 struct stat s{};bool old=stat(path.c_str(),&s)==0;if(old){papa::RemoveLocked(backup.c_str());if(papa::RenameLocked(path.c_str(),backup.c_str()))return false;}
 if(!papa::RenameLocked(temp.c_str(),path.c_str()))return true;
 if(old)papa::RenameLocked(backup.c_str(),path.c_str());
 return false;
}
std::string ReadPlaylist(const std::string&path){FILE*f=OpenUtf8File(path,"rb");if(!f)return {};std::string out;char buffer[8192];size_t n;
 while((n=std::fread(buffer,1,sizeof(buffer),f))){if(out.size()+n>8*1024*1024){papa::CloseLocked(f);return {};}out.append(buffer,n);}bool ok=!std::ferror(f);papa::CloseLocked(f);return ok?out:std::string{};}
}
int RunApp(){
 Logf("LogInit 返回");
 Logf("启动：appletType=%d 电视模式=%d 焦点=%d",int(appletGetAppletType()),int(appletGetOperationMode()),int(appletGetFocusState()));
 Logf("applet 信息已读取");
 LogMemory("启动");
 bool sockets=R_SUCCEEDED(socketInitializeDefault());
 Logf("socketInitializeDefault %s",sockets?"成功":"失败");
 LogStartWatchdog();
 Logf("socket 之后继续");
 Framebuffer fb{};Result rc=framebufferCreate(&fb,nwindowGetDefault(),ScreenWidth,ScreenHeight,PIXEL_FORMAT_RGBA_8888,2);
 Logf("framebufferCreate rc=0x%08x",unsigned(rc));
 if(R_FAILED(rc)){if(sockets)socketExit();WaitExit("Display initialization failed. Press + to exit.");return 1;}
 rc=framebufferMakeLinear(&fb);
 Logf("framebufferMakeLinear rc=0x%08x stride=%u",unsigned(rc),fb.stride);
 if(R_FAILED(rc)){framebufferClose(&fb);if(sockets)socketExit();WaitExit("Display buffer allocation failed. Press + to exit.");return 1;}
 LogMemory("显示初始化后");
 bool plReady=R_SUCCEEDED(plInitialize(PlServiceType_User));
 Logf("plInitialize %s",plReady?"成功":"失败");
 {
  Renderer renderer;bool fontReady=false;
  if(plReady)for(auto type:{PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified,PlSharedFontType_Standard}){
   PlFontData font{};if(R_SUCCEEDED(plGetSharedFontByType(&font,type)))fontReady=renderer.AddFont(font.address,font.size)||fontReady;
  }
  Logf("字体加载 %s",fontReady?"成功":"失败");
  if(!fontReady){framebufferClose(&fb);if(plReady)plExit();if(sockets)socketExit();WaitExit("System font unavailable. Use title takeover (hold R while starting a game). Press + to exit.");return 1;}
  appletSetAutoSleepDisabled(true);CreateDirectories(MusicRoot);CreateDirectories(ConfigRoot);
  Logf("目录就绪，读取配置");
  Settings settings=LoadSettings(Config);LibraryModel model;std::string notice;
  LoadMediaState(ConfigRoot+"/state.json",model.user);
  {MediaState savedResume;if(LoadMediaState(ConfigRoot+"/resume.json",savedResume))model.user.resume=savedResume.resume;}
  LoadSources(ConfigRoot+"/sources.json",model.user.sources);
  Logf("配置读取完成：来源 %zu 个（启用 %zu）",model.user.sources.size(),size_t(std::count_if(model.user.sources.begin(),model.user.sources.end(),[](const MediaSource&s){return s.enabled;})));
  model.SetMode(settings.mode);
  MediaCache cache(ConfigRoot+"/cache");cache.RemoveStartupOrphans();
  Logf("缓存目录就绪，上限 %llu MB",(unsigned long long)(cache.Limit()/1048576));
  {FILE*f=OpenUtf8File(ConfigRoot+"/cache-limit.txt","rb");if(f){unsigned long long limit=0;if(std::fscanf(f,"%llu",&limit)==1&&limit>=64ull*1024*1024&&limit<=4096ull*1024*1024)cache.SetLimit(limit);papa::CloseLocked(f);}}
  MaintenanceWorker maintenance(cache,ConfigRoot,Config);
  AudioPlayer player;player.Volume(settings.volume);ArtworkLoader artwork(cache);ThumbnailLoader thumbnails(cache);
  Page page=Page::Library;SettingsSection settingsSection=SettingsSection::Playback;bool tape=false,stale=false,sourceChosen=false;int selected=0,offset=0,lyricScroll=-1,scrollPixels=0;u64 lastLyricManual=0;
  bool deferredPlay=false;double deferredPosition=0;bool deferredPaused=false;
  if(!sockets)notice="网络初始化失败，SD 卡音乐仍可使用";
  std::string artKey;Thread scanThread{};bool scanThreadReady=false;std::atomic<uint64_t>scanGeneration{0};std::atomic<bool>scanDone{false};bool scanning=false;
  auto joinScan=[&]{if(scanThreadReady){threadWaitForExit(&scanThread);threadClose(&scanThread);scanThreadReady=false;}};
  LibraryLoadResult pending;bool refreshAgain=false,refreshAgainReload=false;uint64_t sourcesRevision=0,scanSourceRevision=0;
  auto pins=[&]{std::vector<std::string>values;for(const auto&s:model.user.sources)if(s.enabled){auto path=CatalogCachePath(cache,s);values.push_back(path);values.push_back(path+".bak");}if(auto t=model.Current()){values.push_back(cache.AssetPath(t->cover));values.push_back(cache.AssetPath(t->lyrics));}return values;};
  auto startScan=[&](bool reloadSources=true){if(scanning){refreshAgain=true;refreshAgainReload=refreshAgainReload||reloadSources;notice="当前刷新结束后将读取最新配置";return;}joinScan();scanSourceRevision=sourcesRevision;
   auto sources=model.user.sources;auto keep=pins();
   scanning=true;scanDone=false;uint64_t generation=++scanGeneration;
   Logf("扫描开始 generation=%llu 来源 %zu 个",(unsigned long long)generation,sources.size());LogMemory("扫描前");
   auto*task=new ScanTask{&pending,sources,&cache,keep,&scanGeneration,generation,&scanDone,reloadSources};
   Result rc=threadCreate(&scanThread,ScanTaskEntry,task,nullptr,0x60000,0x2C,-2);
   Logf("扫描 threadCreate rc=0x%08x generation=%llu",unsigned(rc),(unsigned long long)generation);
   if(R_SUCCEEDED(rc)){rc=threadStart(&scanThread);Logf("扫描 threadStart rc=0x%08x",unsigned(rc));if(R_FAILED(rc))threadClose(&scanThread);}
   scanThreadReady=R_SUCCEEDED(rc);
   if(!scanThreadReady){delete task;scanning=false;notice="无法启动曲库扫描，请稍后重试";}};
  startScan(false);
  auto options=[&]{maintenance.QueueCacheMaintenance(pins());auto usage=cache.Usage();StreamOptions o;o.cacheDirectory=cache.StreamDirectory();o.cacheBytes=usage.bytes<cache.Limit()?cache.Limit()-usage.bytes:0;return o;};
  auto playCurrent=[&](double start=0,bool paused=false){auto t=model.Current();if(!t)return;
   if(t->source==SourceKind::Cloud&&!cache.Usage().ready){deferredPlay=true;deferredPosition=start;deferredPaused=paused;notice="正在准备播放缓存…";return;}
   deferredPlay=false;auto o=options();if(t->source==SourceKind::Cloud&&o.cacheBytes<128*1024){player.Stop();notice="缓存空间不足，请清理闲置缓存或提高上限";return;}player.SetStreamOptions(o);player.Play(t->path,start,paused);lyricScroll=-1;artwork.Request(*t);artKey=t->id+"\n"+t->path;};
  auto change=[&](int direction,bool automatic){if(direction>0?model.Next(automatic):model.Previous())playCurrent();};
  auto save=[&](bool full=true){auto s=player.Snapshot();if(auto t=model.Current()){if(!s.loading&&s.error.empty()&&s.path==t->path){model.user.resume={*t,s.seconds,model.GetMode(),settings.volume,true};}}
   maintenance.QueueSave(settings,model.user,full);
  };
  auto resume=[&]{auto r=model.user.resume;if(!r.valid)return;
   const auto&catalog=r.track.source==SourceKind::Cloud?model.Cloud():model.Local();
   auto fresh=std::find_if(catalog.begin(),catalog.end(),[&](const Track&t){return t.id==r.track.id&&t.path==r.track.path;});
   if(fresh!=catalog.end())r.track=*fresh;
   model.Enqueue(r.track);model.StartQueue(model.Queued().size()-1);settings.mode=r.mode;settings.volume=r.volume;model.SetMode(r.mode);player.Volume(r.volume);playCurrent(r.position,false);notice=r.position>0?"正在恢复上次位置，云端歌曲可能需要等待缓存":"已打开上次歌曲";page=Page::Player;};
  auto volume=[&](int delta){settings.volume=std::max(0,std::min(100,settings.volume+delta));player.Volume(settings.volume);save(false);};
  std::vector<Track>visible;std::vector<LibraryGroup>groups;std::string libraryHeading;
  std::map<std::string,int>visibleIndex;std::vector<const Track*>groupArt;
  auto count=[&]()->int{if(page==Page::Settings)return settingsSection==SettingsSection::Sources?int(model.user.sources.size()):SettingCount(settingsSection);if(page==Page::Queue)return model.Queued().size();if(page==Page::Library&&model.ShowingGroups())return groups.size();return visible.size();};
  auto rows=[&]{return page==Page::Settings?(settingsSection==SettingsSection::Sources?SourceVisibleRows:SettingCount(settingsSection)):VisibleRows;};
  auto columns=[&]{return page==Page::Library&&model.ShowingGroups()?GroupColumns:1;};
  auto reveal=[&]{int cols=columns(),capacity=rows();selected=std::max(0,std::min(selected,count()-1));int row=selected/cols,first=offset/cols;
   if(row<first)first=row;
   if(row>=first+capacity/cols)first=row-capacity/cols+1;
   int maxFirst=std::max(0,(count()+cols-1)/cols-capacity/cols);offset=std::max(0,std::min(first,maxFirst))*cols;};
  auto rebuild=[&]{visible=page==Page::Queue?model.Queued():model.Visible();groups=model.ShowingGroups()?model.Groups():std::vector<LibraryGroup>{};visibleIndex.clear();groupArt.clear();
   libraryHeading.clear();
   if(!model.groupKey.empty()){
    if(model.source==LibrarySource::Playlists){for(const auto&p:model.user.playlists)if(p.id==model.groupKey){libraryHeading=p.name;break;}}
    else if(model.grouping==Grouping::Albums){auto split=model.groupKey.find('\n');libraryHeading=split==std::string::npos?model.groupKey:model.groupKey.substr(split+1);}
    else libraryHeading=model.groupKey;
   }
   std::map<std::string,const Track*>representatives;for(size_t i=0;i<visible.size();++i){const auto&t=visible[i];visibleIndex[t.id]=int(i);auto key=model.grouping==Grouping::Albums?(t.albumArtist.empty()?t.artist:t.albumArtist)+"\n"+t.album:t.artist;representatives.emplace(key,&t);}
   if(model.source==LibrarySource::Playlists)for(const auto&p:model.user.playlists)if(!p.tracks.empty())representatives[p.id]=&p.tracks.front();
   for(const auto&g:groups){auto it=representatives.find(g.key);groupArt.push_back(it==representatives.end()?nullptr:it->second);}reveal();};
  auto chosen=[&]()->const Track*{return selected>=0&&size_t(selected)<visible.size()?&visible[selected]:nullptr;};
  auto resetSelection=[&]{selected=page==Page::Queue?std::max(0,model.QueueIndex()):0;offset=scrollPixels=0;rebuild();};
  auto sourceEditor=[&](int index){MediaSource s;if(index>=0&&size_t(index)<model.user.sources.size())s=model.user.sources[index];
   auto url=Keyboard("云端 JSON / M3U 地址",s.url);if(url.empty())return;
   if(url.compare(0,7,"http://")&&url.compare(0,8,"https://")){notice="请输入 HTTP 或 HTTPS 地址";return;}
   auto name=Keyboard("音乐源名称",s.name.empty()?"云端音乐库":s.name);if(name.empty())return;
   if(s.id.empty())s.id="source-"+std::to_string(svcGetSystemTick());
   s.url=url;s.name=name;s.enabled=true;
   if(index>=0&&size_t(index)<model.user.sources.size())model.user.sources[index]=s;else model.user.sources.push_back(s);
   ++sourcesRevision;maintenance.QueueSources(model.user.sources);startScan(false);save();};
  auto importPlaylist=[&]{auto path=Keyboard("导入 M3U8 文件路径","sdmc:/music/playlist.m3u8");if(path.empty())return;
   auto text=ReadPlaylist(path);std::string error;auto tracks=ParseCatalog(text,"","import",error);if(!error.empty()||tracks.empty()){notice=error.empty()?"歌单文件为空或不可读取":error;return;}
   auto name=Keyboard("歌单名称","导入歌单");if(name.empty())return;std::string id="import-"+std::to_string(svcGetSystemTick());
   model.user.playlists.push_back({id,name,std::move(tracks)});save();notice="已导入歌单";};
  auto act=[&](Hit hit){auto state=player.Snapshot();
   if(hit.action==Action::None)return;
   if(hit.action>=Action::PageLibrary&&hit.action<=Action::PageSettings){page=static_cast<Page>(int(hit.action)-int(Action::PageLibrary));resetSelection();return;}
   if(hit.action>=Action::SettingsPlayback&&hit.action<=Action::SettingsPlaylists){settingsSection=static_cast<SettingsSection>(int(hit.action)-int(Action::SettingsPlayback));selected=offset=scrollPixels=0;return;}
   if(hit.action>=Action::SourceSD&&hit.action<=Action::SourcePlaylists){sourceChosen=true;model.SelectSource(static_cast<LibrarySource>(int(hit.action)-int(Action::SourceSD)));resetSelection();return;}
   if(hit.action>=Action::GroupSongs&&hit.action<=Action::GroupArtists){model.SelectGrouping(static_cast<Grouping>(int(hit.action)-int(Action::GroupSongs)));resetSelection();return;}
   switch(hit.action){
   case Action::Song:if(model.StartVisible(hit.index)){selected=hit.index;playCurrent();page=Page::Player;scrollPixels=0;}break;
   case Action::GroupItem:if(hit.index>=0&&size_t(hit.index)<groups.size()){model.groupKey=groups[hit.index].key;resetSelection();}break;
   case Action::QueueSong:if(model.StartQueue(hit.index)){playCurrent();selected=hit.index;}break;
   case Action::QueueSelect:selected=hit.index;reveal();break;
   case Action::CycleSource:sourceChosen=true;model.SelectSource(static_cast<LibrarySource>((int(model.source)+1)%4));resetSelection();break;
   case Action::CycleTheme:settings.theme=hit.index>=0?NormalizeTheme(hit.index):NextTheme(settings.theme);save(false);break;
   case Action::CycleGrouping:model.SelectGrouping(static_cast<Grouping>((int(model.grouping)+1)%3));resetSelection();break;
   case Action::Toggle:if(model.Current()){if(!state.error.empty())playCurrent();else player.Pause(!state.paused);}else if(!model.ShowingGroups()&&model.StartVisible(selected))playCurrent();break;
   case Action::Previous:change(-1,false);break;case Action::Next:change(1,false);break;
   case Action::Seek:if(state.seekable){double duration=state.duration>0?state.duration:(model.Current()?model.Current()->duration:0);if(duration>0)player.Seek(duration*std::max(0.0,std::min(1.0,hit.fraction)));}break;
   case Action::VolumeDown:volume(-5);break;case Action::VolumeUp:volume(5);break;
   case Action::Mode:settings.mode=static_cast<Mode>((int(settings.mode)+1)%4);model.SetMode(settings.mode);save(false);break;
   case Action::Search:{auto text=Keyboard("搜索歌曲、歌手、专辑",model.query);model.query=text;resetSelection();break;}
   case Action::Back:if(page==Page::Lyrics||page==Page::Queue){page=Page::Player;lyricScroll=-1;}else if(!model.query.empty())model.query.clear();else model.groupKey.clear();resetSelection();break;
   case Action::Enqueue:if(!model.ShowingGroups())if(auto t=chosen()){model.Enqueue(*t);notice="已加入播放队列";}break;
   case Action::Favorite:{const Track*t=page==Page::Library?(!model.ShowingGroups()?chosen():nullptr):model.Current();if(t){model.ToggleFavorite(*t);save();}}break;
   case Action::Tape:tape=!tape;break;case Action::Refresh:thumbnails.Clear(true);if(auto t=model.Current())artwork.Request(*t,true);startScan();break;
   case Action::SourceAdd:sourceEditor(-1);break;case Action::SourceEdit:sourceEditor(hit.index);break;
   case Action::SourceDelete:if(hit.index>=0&&size_t(hit.index)<model.user.sources.size()){model.user.sources.erase(model.user.sources.begin()+hit.index);++sourcesRevision;maintenance.QueueSources(model.user.sources);startScan(false);save();}break;
   case Action::CacheDown:case Action::CacheUp:{int64_t value=int64_t(cache.Limit())+(hit.action==Action::CacheUp?64ll:-64ll)*1024*1024;cache.SetLimit(std::max<int64_t>(64ll*1024*1024,std::min<int64_t>(4096ll*1024*1024,value)));maintenance.QueueCacheMaintenance(pins(),false,true);break;}
   case Action::ClearCache:maintenance.QueueCacheMaintenance(pins(),true);notice="正在清理闲置缓存，当前播放数据会保留";break;
   case Action::Resume:resume();break;
   case Action::SavePlaylist:{auto name=Keyboard("保存队列为歌单","我的歌单");if(!model.SaveQueueAsPlaylist(name).empty()){save();notice="已保存歌单";}break;}
   case Action::ImportPlaylist:importPlaylist();break;
   case Action::ExportPlaylist:{auto path=Keyboard("导出 M3U8 文件路径","sdmc:/music/playlist.m3u8");if(!path.empty()){auto dot=path.find_last_of('.');auto extension=dot==std::string::npos?std::string{}:path.substr(dot);for(auto&c:extension)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(extension!=".m3u"&&extension!=".m3u8"){notice="请选择 .m3u8 文件，歌曲原文件不会被替换";break;}MediaPlaylist p{"export","播放队列",model.Queued()};notice=WritePlaylist(path,ExportPlaylist(p))?"歌单已导出":"导出失败，请检查目录和剩余空间";}break;}
   case Action::DeletePlaylist:if(model.source==LibrarySource::Playlists){auto key=model.groupKey;int index=hit.index>=0?hit.index:selected;if(key.empty()&&index>=0&&size_t(index)<groups.size())key=groups[index].key;model.user.playlists.erase(std::remove_if(model.user.playlists.begin(),model.user.playlists.end(),[&](const MediaPlaylist&p){return p.id==key;}),model.user.playlists.end());model.groupKey.clear();save();}break;
   case Action::QueueRemove:{int before=model.QueueIndex();if(model.RemoveQueued(hit.index)&&before==hit.index){if(model.Current())playCurrent();else player.Stop();}break;}
   case Action::QueueUp:model.MoveQueued(hit.index,-1);selected=std::max(0,hit.index-1);break;
   case Action::QueueDown:if(model.MoveQueued(hit.index,1))selected=hit.index+1;break;
   case Action::LyricEarlier:case Action::LyricLater:case Action::LyricReset:if(auto t=model.Current()){auto&n=model.user.lyricsOffsets[t->id];n=hit.action==Action::LyricReset?0:std::max(-3600.0,std::min(3600.0,n+(hit.action==Action::LyricEarlier?.5:-.5)));save();}break;
   default:break;
   }if(NeedsLibraryRebuild(hit.action))rebuild();
  };
  // libnx __appInit already initializes HID. The old NULL devoptab/HID faults
  // were teardown racing the main thread after HTTP detach threw ENOSYS.
  PadState pad{};
  padConfigureInput(1,HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  Logf("pad 初期化完了");
  hidInitializeTouchScreen();
  Logf("hidInitializeTouchScreen 完了");
  Logf("进入主循环");
  u64 lastFrame=svcGetSystemTick(),lastRepeat=0,lastSave=lastFrame,lastPaint=0,lastStatLog=lastFrame,lastMetrics=lastFrame;
  uint64_t handledEnd=0;
  u64 frames=0,frameFailures=0;FrameMetrics metrics;TouchInput pointer;LyricMotion lyricMotion;double lastPaintLyricPosition=-1;
  int lastX=0,lastY=0;std::string lastNotice;u64 noticeSince=lastFrame;
  std::shared_ptr<const Track> appliedMetadata;std::shared_ptr<Lyrics> countedLyrics;int lyricRows=0;
  rebuild();
  while(appletMainLoop()){
   u64 begin=svcGetSystemTick();
   bool changed=false;
   if(scanning&&scanDone.load(std::memory_order_acquire)){
    joinScan();scanning=false;
    Logf("扫描完成：SD %zu 首，云端 %zu 首，提示=%s",pending.sd.size(),pending.cloud.size(),pending.notice.c_str());
    model.SetTracks(SourceKind::SD,std::move(pending.sd));model.SetTracks(SourceKind::Cloud,std::move(pending.cloud));
    if(scanSourceRevision==sourcesRevision)model.user.sources=std::move(pending.sources);
    stale=pending.stale;notice=pending.notice;
    if(!sourceChosen&&model.Local().empty()&&!model.Cloud().empty()&&model.source==LibrarySource::SD)model.SelectSource(LibrarySource::Cloud);
    if(const auto*current=model.Current()){
     const auto&catalog=current->source==SourceKind::Cloud?model.Cloud():model.Local();
     auto fresh=std::find_if(catalog.begin(),catalog.end(),[&](const Track&t){return t.id==current->id&&t.path==current->path;});
     if(fresh!=catalog.end()){Track copy=*fresh;model.UpdateTrackMetadata(copy);artwork.Request(*model.Current());}
    }
    rebuild();changed=true;
    if(refreshAgain){bool reload=refreshAgainReload;refreshAgain=refreshAgainReload=false;startScan(reload);}
   }
   if(deferredPlay&&cache.Usage().ready){playCurrent(deferredPosition,deferredPaused);changed=true;}
   auto state=player.Snapshot();
   if(state.ended&&state.completion!=handledEnd){handledEnd=state.completion;change(1,true);state=player.Snapshot();changed=true;}
   if(auto t=model.Current())if(artKey!=t->id+"\n"+t->path){artKey=t->id+"\n"+t->path;artwork.Request(*t);lyricScroll=-1;changed=true;}
   ArtworkSnapshot art;
   auto readArt=[&]{art=artwork.Snapshot();auto t=model.Current();if(!t||art.trackId!=t->id||art.trackPath!=t->path)art={};};
   readArt();
   if(art.enrichedTrack&&art.enrichedTrack!=appliedMetadata){
    appliedMetadata=art.enrichedTrack;
    if(model.UpdateTrackMetadata(*art.enrichedTrack)){rebuild();maintenance.QueueCacheMaintenance(pins());changed=true;}
   }
   if(art.lyrics!=countedLyrics){countedLyrics=art.lyrics;lyricRows=!art.lyrics?0:art.lyrics->lines.empty()?int(WrapPlainLyrics(art.lyrics->plain).size()):int(art.lyrics->lines.size());changed=true;}
   if(lyricScroll>=0&&armTicksToNs(begin-lastLyricManual)>=6000000000ull&&!pointer.Held()){lyricScroll=-1;scrollPixels=0;changed=true;}
   auto scrollLyrics=[&](int delta){double shift=0;if(auto t=model.Current()){auto it=model.user.lyricsOffsets.find(t->id);if(it!=model.user.lyricsOffsets.end())shift=it->second;}
    int first=lyricScroll>=0?lyricScroll:std::max(0,art.lyrics?art.lyrics->Current(state.seconds,shift)-2:0);
    lyricScroll=std::clamp(first+delta,0,std::max(0,lyricRows-1));lastLyricManual=begin;
   };
   bool inputReady=hidGetSharedmemAddr()!=nullptr;u64 down=0,held=0;
   if(inputReady){padUpdate(&pad);down=padGetButtonsDown(&pad);held=padGetButtons(&pad);}
   if(down&HidNpadButton_Plus)break;
   constexpr u64 arrows=HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right;
   u64 nav=down;
   if(down&arrows)lastRepeat=begin+armNsToTicks(250000000);
   else if(begin>lastRepeat&&(held&arrows)){nav|=held&arrows;lastRepeat=begin+armNsToTicks(80000000);}
   if(down&HidNpadButton_Minus){page=NextMainPage(page);resetSelection();pointer.Cancel();}
   if(nav&HidNpadButton_Up){if(page==Page::Lyrics)scrollLyrics(-1);else{selected-=columns();scrollPixels=0;reveal();}}
   if(nav&HidNpadButton_Down){if(page==Page::Lyrics)scrollLyrics(1);else{selected+=columns();scrollPixels=0;reveal();}}
   if(nav&(HidNpadButton_Left|HidNpadButton_Right)){
    int direction=(nav&HidNpadButton_Right)?1:-1;
    if(page==Page::Lyrics)act({direction>0?Action::LyricLater:Action::LyricEarlier});
    else if(page==Page::Library){if(model.ShowingGroups()){selected+=direction;scrollPixels=0;reveal();}else{sourceChosen=true;model.SelectSource(static_cast<LibrarySource>((int(model.source)+direction+4)%4));resetSelection();}}
    else volume(direction*5);
   }
   if(down&HidNpadButton_A){View input;input.page=page;input.selected=selected;input.settingsSection=settingsSection;input.sources=&model.user.sources;input.showingGroups=model.ShowingGroups();act(ConfirmAction(input));}
   if(down&HidNpadButton_B){if(page==Page::Library)act({Action::Back});else{page=ParentPage(page);lyricScroll=-1;resetSelection();pointer.Cancel();}}
   if(down&HidNpadButton_X){if(page==Page::Library)act({Action::Enqueue});else if(page==Page::Queue)act({Action::QueueRemove,selected});else if(page==Page::Settings){if(settingsSection==SettingsSection::Sources)act({Action::SourceAdd});}else if(page==Page::Lyrics)act({Action::LyricReset});else act({Action::PageQueue});}
   if(down&HidNpadButton_Y){View input;input.page=page;input.selected=selected;input.librarySource=model.source;input.settingsSection=settingsSection;input.showingGroups=model.ShowingGroups();act(SecondaryAction(input));}
   if(down&HidNpadButton_L)change(-1,false);
   if(down&HidNpadButton_R)change(1,false);
   if(down&(HidNpadButton_ZL|HidNpadButton_ZR)){
    int direction=(down&HidNpadButton_ZR)?1:-1;
    if(page==Page::Library){sourceChosen=true;model.SelectSource(static_cast<LibrarySource>((int(model.source)+direction+4)%4));resetSelection();}
    else if(page==Page::Queue)act({direction>0?Action::QueueDown:Action::QueueUp,selected});
    else if(page==Page::Settings){settingsSection=static_cast<SettingsSection>((int(settingsSection)+direction+4)%4);selected=offset=scrollPixels=0;pointer.Cancel();}
    else if(state.seekable)player.Seek(direction>0?(state.durationKnown?std::min(state.duration,state.seconds+10):state.seconds+10):std::max(0.0,state.seconds-10));
   }
   if(down&HidNpadButton_StickR){
    if(page==Page::Library)act({Action::CycleGrouping});
    else if(page==Page::Settings)act({Action::CycleTheme});
   }
   if(page==Page::Library&&down&HidNpadButton_StickL)act({Action::Search});
   state=player.Snapshot();readArt();
   if(notice!=lastNotice){lastNotice=notice;noticeSince=begin;changed=true;}
   if(!notice.empty()&&!stale&&((notice.find("正在恢复上次位置")==0&&!state.loading&&!state.buffering&&state.error.empty()&&!state.path.empty())||armTicksToNs(begin-noticeSince)>5000000000ull)){notice.clear();lastNotice.clear();changed=true;}
   auto compose=[&]{
    View view;view.page=page;view.settingsSection=settingsSection;view.tracks=page==Page::Library?&visible:&model.Queued();view.groups=&groups;view.sources=&model.user.sources;view.playing=model.Current();
    if(view.playing){if(page==Page::Library){auto it=visibleIndex.find(view.playing->id);if(it!=visibleIndex.end())view.current=it->second;}else view.current=model.QueueIndex();}
    view.selected=selected;view.offset=offset;view.scrollPixels=scrollPixels;view.volume=settings.volume;view.mode=settings.mode;view.theme=settings.theme;view.librarySource=model.source;view.grouping=model.grouping;view.showingGroups=page==Page::Library&&model.ShowingGroups();view.query=model.query;view.subtitle=libraryHeading;
    view.paused=state.paused;view.loading=state.loading||deferredPlay;view.scanning=scanning;view.seconds=state.seconds;view.duration=state.duration>0?state.duration:(view.playing?view.playing->duration:0);view.error=state.error;view.ended=state.ended;view.sourceRate=state.rate;view.seekable=state.seekable;view.durationKnown=state.durationKnown||view.duration>0;view.buffering=state.buffering;
    auto health=maintenance.Snapshot();if(view.error.empty()&&!health.error.empty())view.error=health.error;
    view.tape=tape;view.stale=stale;view.notice=notice.empty()?art.note:notice;view.resumeAvailable=model.user.resume.valid&&state.path.empty();view.favorite=view.playing&&model.Favorite(*view.playing);
    view.coverImage=art.cover.get();view.lyricData=art.lyrics.get();view.artLoading=art.loading;view.lyricScroll=lyricScroll;
    if(view.playing){auto it=model.user.lyricsOffsets.find(view.playing->id);if(it!=model.user.lyricsOffsets.end())view.lyricOffset=it->second;}
    view.cacheBytes=cache.Usage().bytes;view.cacheLimit=cache.Limit();auto network=player.StreamSnapshotValue();view.networkBytes=network.downloadedBytes;view.networkTotal=network.totalBytes;
    if(page==Page::Library||page==Page::Queue)for(int row=0;row<VisibleRows+GroupColumns;++row){int i=offset+row;const Track*track=nullptr;
     if(view.showingGroups){if(i>=0&&size_t(i)<groupArt.size())track=groupArt[i];}
     else if(i>=0&&size_t(i)<view.tracks->size())track=&(*view.tracks)[i];
     view.thumbnails.push_back(track?thumbnails.Get(*track):nullptr);
    }
    return view;
   };
   View view=compose();HidTouchScreenState touch{};bool hasTouch=inputReady&&hidGetTouchScreenStates(&touch,1)>0&&touch.count>0;
   if(hasTouch){lastX=touch.touches[0].x;lastY=touch.touches[0].y;}
   bool wasHeld=pointer.Held();auto gesture=pointer.Update(hasTouch,lastX,lastY,view);
   if(gesture.scrolling){
    scrollPixels+=gesture.deltaY;
    if(gesture.lyrics){
     if(lyricScroll<0)scrollLyrics(0);
     while(scrollPixels<=-LyricRowHeight&&lyricScroll<std::max(0,lyricRows-1)){scrollLyrics(1);scrollPixels+=LyricRowHeight;}
     while(scrollPixels>=LyricRowHeight&&lyricScroll>0){scrollLyrics(-1);scrollPixels-=LyricRowHeight;}
     scrollPixels=std::clamp(scrollPixels,-LyricRowHeight,LyricRowHeight);lastLyricManual=begin;
    }else{
     int cols=columns(),height=cols>1?GroupRowHeight:page==Page::Settings?SourceRowHeight:ListRowHeight;
     int maxOffset=std::max(0,(count()+cols-1)/cols-rows()/cols)*cols;
     while(scrollPixels<=-height&&offset<maxOffset){offset+=cols;scrollPixels+=height;}
     while(scrollPixels>=height&&offset>0){offset-=cols;scrollPixels-=height;}
     scrollPixels=std::clamp(scrollPixels,-height,height);selected=std::clamp(selected,offset,std::max(offset,std::min(count()-1,offset+rows()-1)));
    }
    changed=true;
   }
   if(gesture.action.action!=Action::None){act(gesture.action);state=player.Snapshot();readArt();changed=true;}
   if(!hasTouch&&scrollPixels){scrollPixels=std::abs(scrollPixels)<=2?0:int(scrollPixels*.6);changed=true;}
   view=compose();view.seeking=gesture.preview;view.seekFraction=gesture.fraction;
   if(pointer.Held()&&!pointer.Dragging()){view.touchX=pointer.X();view.touchY=pointer.Y();}
   auto motion=lyricMotion.Update(view,armTicksToNs(begin)/1e9);
   view.lyricPosition=motion.position;view.lyricAnimating=motion.active;
   if(appletGetFocusState()!=AppletFocusState_InFocus){pointer.Cancel();svcSleepThread(50000000);continue;}
   // Scroll lyrics at input cadence only during transitions; idle pages keep
   // the existing 100 ms refresh interval. Paint the final settled frame too.
   u64 drawNs=0;bool paint=changed||down||nav||pointer.Held()||wasHeld||view.lyricAnimating||view.lyricPosition!=lastPaintLyricPosition||armTicksToNs(begin-lastPaint)>=100000000ull;
   if(paint){
    u32 stride=0;auto pixels=TryBeginFrame(fb,&stride);
    if(pixels){u64 stamp=svcGetSystemTick();renderer.Draw(pixels,stride,view);drawNs=armTicksToNs(svcGetSystemTick()-stamp);framebufferEnd(&fb);++frames;lastPaint=begin;lastPaintLyricPosition=view.lyricPosition;}
    else{++frameFailures;if(frameFailures<4||(frameFailures%60)==0)Logf("取帧失败 累计=%llu",(unsigned long long)frameFailures);}
   }
   if(armTicksToNs(begin-lastStatLog)>5000000000ull){lastStatLog=begin;Logf("运行中 frame=%llu 页=%d 取帧失败=%llu art=%d lyrics=%zu",(unsigned long long)frames,int(page),(unsigned long long)frameFailures,int(bool(art.cover)),art.lyrics?art.lyrics->lines.size():0);}
   if(armTicksToNs(begin-lastSave)>15000000000ull){save(false);lastSave=begin;}
   u64 elapsed=armTicksToNs(svcGetSystemTick()-begin);metrics.Add(elapsed,drawNs);
   if(armTicksToNs(begin-lastMetrics)>10000000000ull){metrics.Report();lastMetrics=begin;}
   elapsed=armTicksToNs(svcGetSystemTick()-begin);if(elapsed<16666667)svcSleepThread(16666667-elapsed);
  }
  Logf("退出主循环 frames=%llu 取帧失败=%llu",(unsigned long long)frames,(unsigned long long)frameFailures);
  ++scanGeneration;joinScan();save();player.Stop();maintenance.Shutdown();metrics.Report();appletSetAutoSleepDisabled(false);
 }
 ShutdownHttpWorkers();framebufferClose(&fb);if(plReady)plExit();if(sockets)socketExit();return 0;
}

int main(int,char**){
 LogInit();
 LogInstallExitHooks(); // terminate/atexit 钩子：静默退出时也能留下原因
 int rc=RunApp();
 Logf("RunApp 返回 %d",rc);
 LogClose();
 return rc;
}
