#include <switch.h>
#include "audio.hpp"
#include "ui.hpp"
#include "runtime.hpp"
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
std::string Keyboard(const char*hint,const std::string&initial={}){
 SwkbdConfig keyboard{};if(R_FAILED(swkbdCreate(&keyboard,0)))return {};
 swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetHeaderText(&keyboard,hint);
 swkbdConfigSetInitialText(&keyboard,initial.c_str());swkbdConfigSetStringLenMax(&keyboard,2048);
 char output[8192]{};Result rc=swkbdShow(&keyboard,output,sizeof(output));swkbdClose(&keyboard);
 return R_SUCCEEDED(rc)?std::string(output):std::string{};
}
bool WritePlaylist(const std::string&path,const std::string&text){
 auto temp=path+".tmp",backup=path+".bak";FILE*f=OpenUtf8File(temp,"wb");if(!f)return false;
 bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();ok=std::fclose(f)==0&&ok;if(!ok){std::remove(temp.c_str());return false;}
 struct stat s{};bool old=stat(path.c_str(),&s)==0;if(old){std::remove(backup.c_str());if(std::rename(path.c_str(),backup.c_str()))return false;}
 if(!std::rename(temp.c_str(),path.c_str()))return true;
 if(old)std::rename(backup.c_str(),path.c_str());
 return false;
}
std::string ReadPlaylist(const std::string&path){FILE*f=OpenUtf8File(path,"rb");if(!f)return {};std::string out;char buffer[8192];size_t n;
 while((n=std::fread(buffer,1,sizeof(buffer),f))){if(out.size()+n>8*1024*1024){std::fclose(f);return {};}out.append(buffer,n);}bool ok=!std::ferror(f);std::fclose(f);return ok?out:std::string{};}
}
int main(int,char**){
 bool sockets=R_SUCCEEDED(socketInitializeDefault());
 Framebuffer fb{};Result rc=framebufferCreate(&fb,nwindowGetDefault(),ScreenWidth,ScreenHeight,PIXEL_FORMAT_RGBA_8888,2);
 if(R_FAILED(rc)){if(sockets)socketExit();WaitExit("Display initialization failed. Press + to exit.");return 1;}
 rc=framebufferMakeLinear(&fb);
 if(R_FAILED(rc)){framebufferClose(&fb);if(sockets)socketExit();WaitExit("Display buffer allocation failed. Press + to exit.");return 1;}
 bool plReady=R_SUCCEEDED(plInitialize(PlServiceType_User));
 {
  Renderer renderer;bool fontReady=false;
  if(plReady)for(auto type:{PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified,PlSharedFontType_Standard}){
   PlFontData font{};if(R_SUCCEEDED(plGetSharedFontByType(&font,type)))fontReady=renderer.AddFont(font.address,font.size)||fontReady;
  }
  if(!fontReady){framebufferClose(&fb);if(plReady)plExit();if(sockets)socketExit();WaitExit("System font unavailable. Use title takeover (hold R while starting a game). Press + to exit.");return 1;}
  appletSetAutoSleepDisabled(true);CreateDirectories(MusicRoot);CreateDirectories(ConfigRoot);
  Settings settings=LoadSettings(Config);LibraryModel model;std::string notice;
  LoadMediaState(ConfigRoot+"/state.json",model.user);
  {MediaState savedResume;if(LoadMediaState(ConfigRoot+"/resume.json",savedResume))model.user.resume=savedResume.resume;}
  LoadSources(ConfigRoot+"/sources.json",model.user.sources);
  model.SetMode(settings.mode);
  MediaCache cache(ConfigRoot+"/cache");cache.RemoveStartupOrphans();
  {FILE*f=OpenUtf8File(ConfigRoot+"/cache-limit.txt","rb");if(f){unsigned long long limit=0;if(std::fscanf(f,"%llu",&limit)==1&&limit>=64ull*1024*1024&&limit<=4096ull*1024*1024)cache.SetLimit(limit);std::fclose(f);}}
  AudioPlayer player;player.Volume(settings.volume);ArtworkLoader artwork(cache);ThumbnailLoader thumbnails(cache);
  Page page=Page::Library;bool tape=false,stale=false;int selected=0,offset=0,lyricScroll=-1;u64 lastLyricManual=0;
  if(!sockets)notice="网络初始化失败，SD 卡音乐仍可使用";
  std::string artKey;std::thread scanner;std::atomic<uint64_t>scanGeneration{0};std::atomic<bool>scanDone{false};bool scanning=false;
  LibraryLoadResult pending;bool refreshAgain=false;
  auto pins=[&]{std::vector<std::string>values;for(const auto&s:model.user.sources)if(s.enabled)values.push_back(CatalogCachePath(cache,s));if(auto t=model.Current()){values.push_back(cache.AssetPath(t->cover));values.push_back(cache.AssetPath(t->lyrics));}return values;};
  auto startScan=[&]{if(scanning){refreshAgain=true;notice="当前刷新结束后将读取最新配置";return;}if(scanner.joinable())scanner.join();
   LoadSources(ConfigRoot+"/sources.json",model.user.sources);auto sources=model.user.sources;auto keep=pins();
   cache.SetPins(keep);scanning=true;scanDone=false;uint64_t generation=++scanGeneration;
   scanner=std::thread([&,sources,keep,generation]{pending=LoadLibraries(MusicRoot,ConfigRoot,cache,sources,scanGeneration,generation,keep);scanDone.store(true,std::memory_order_release);});};
  startScan();
  auto options=[&]{cache.SetPins(pins());cache.Prune(pins());auto used=cache.Usage().bytes;StreamOptions o;o.cacheDirectory=cache.StreamDirectory();o.cacheBytes=used<cache.Limit()?cache.Limit()-used:0;return o;};
  auto playCurrent=[&](double start=0,bool paused=false){auto t=model.Current();if(!t)return;auto o=options();if(t->source==SourceKind::Cloud&&o.cacheBytes<128*1024){player.Stop();notice="缓存空间不足，请清理闲置缓存或提高上限";return;}player.SetStreamOptions(o);player.Play(t->path,start,paused);lyricScroll=-1;artwork.Request(*t);artKey=t->id;};
  auto change=[&](int direction,bool automatic){if(direction>0?model.Next(automatic):model.Previous())playCurrent();};
  auto save=[&](bool full=true){auto s=player.Snapshot();if(auto t=model.Current()){if(!s.loading&&s.error.empty()&&s.path==t->path){model.user.resume={*t,s.seconds,model.GetMode(),settings.volume,true};}}
   std::string error;if(full&&!SaveMediaState(ConfigRoot+"/state.json",model.user,&error))notice=error;
   MediaState resumeOnly;resumeOnly.resume=model.user.resume;
   if(!SaveMediaState(ConfigRoot+"/resume.json",resumeOnly,&error))notice=error;
   if(!SaveSettings(Config,settings))notice="设置保存失败，请检查 SD 卡";
  };
  auto resume=[&]{auto r=model.user.resume;if(!r.valid)return;model.Enqueue(r.track);model.StartQueue(model.Queued().size()-1);settings.mode=r.mode;settings.volume=r.volume;model.SetMode(r.mode);player.Volume(r.volume);playCurrent(r.position,false);notice=r.position>0?"正在恢复上次位置，云端歌曲可能需要等待缓存":"已打开上次歌曲";page=Page::Player;};
  auto volume=[&](int delta){settings.volume=std::max(0,std::min(100,settings.volume+delta));player.Volume(settings.volume);};
  std::vector<Track>visible;std::vector<LibraryGroup>groups;
  auto count=[&]()->int{if(page==Page::Settings)return model.user.sources.size();if(page==Page::Queue)return model.Queued().size();if(page==Page::Library&&model.ShowingGroups())return groups.size();return visible.size();};
  auto reveal=[&]{int rows=page==Page::Settings?4:VisibleRows;selected=std::max(0,std::min(selected,count()-1));if(selected<offset)offset=selected;if(selected>=offset+rows)offset=selected-rows+1;offset=std::max(0,std::min(offset,std::max(0,count()-rows)));};
  auto rebuild=[&]{visible=page==Page::Queue?model.Queued():model.Visible();groups=model.Groups();reveal();};
  auto chosen=[&]()->const Track*{return selected>=0&&size_t(selected)<visible.size()?&visible[selected]:nullptr;};
  auto resetSelection=[&]{selected=offset=0;rebuild();};
  auto sourceEditor=[&](int index){MediaSource s;if(index>=0&&size_t(index)<model.user.sources.size())s=model.user.sources[index];
   auto url=Keyboard("云端 JSON / M3U 地址",s.url);if(url.empty())return;
   if(url.compare(0,7,"http://")&&url.compare(0,8,"https://")){notice="请输入 HTTP 或 HTTPS 地址";return;}
   auto name=Keyboard("音乐源名称",s.name.empty()?"云端音乐库":s.name);if(name.empty())return;
   if(s.id.empty())s.id="source-"+std::to_string(svcGetSystemTick());
   s.url=url;s.name=name;s.enabled=true;
   if(index>=0&&size_t(index)<model.user.sources.size())model.user.sources[index]=s;else model.user.sources.push_back(s);
   std::string error;if(!SaveSources(ConfigRoot+"/sources.json",model.user.sources,&error)){notice=error;return;}startScan();save();};
  auto importPlaylist=[&]{auto path=Keyboard("导入 M3U8 文件路径","sdmc:/music/playlist.m3u8");if(path.empty())return;
   auto text=ReadPlaylist(path);std::string error;auto tracks=ParseCatalog(text,"","import",error);if(!error.empty()||tracks.empty()){notice=error.empty()?"歌单文件为空或不可读取":error;return;}
   auto name=Keyboard("歌单名称","导入歌单");if(name.empty())return;std::string id="import-"+std::to_string(svcGetSystemTick());
   model.user.playlists.push_back({id,name,std::move(tracks)});save();notice="已导入歌单";};
  auto act=[&](Hit hit){auto state=player.Snapshot();
   if(hit.action>=Action::PageLibrary&&hit.action<=Action::PageSettings){page=static_cast<Page>(int(hit.action)-int(Action::PageLibrary));resetSelection();return;}
   if(hit.action>=Action::SourceSD&&hit.action<=Action::SourcePlaylists){model.SelectSource(static_cast<LibrarySource>(int(hit.action)-int(Action::SourceSD)));resetSelection();return;}
   if(hit.action>=Action::GroupSongs&&hit.action<=Action::GroupArtists){model.SelectGrouping(static_cast<Grouping>(int(hit.action)-int(Action::GroupSongs)));resetSelection();return;}
   switch(hit.action){
   case Action::Song:if(model.StartVisible(hit.index)){selected=hit.index;playCurrent();}break;
   case Action::GroupItem:if(hit.index>=0&&size_t(hit.index)<groups.size()){model.groupKey=groups[hit.index].key;resetSelection();}break;
   case Action::QueueSong:if(model.StartQueue(hit.index)){playCurrent();selected=hit.index;}break;
   case Action::Toggle:if(model.Current()){if(!state.error.empty())playCurrent();else player.Pause(!state.paused);}else if(!model.ShowingGroups()&&model.StartVisible(selected))playCurrent();break;
   case Action::Previous:change(-1,false);break;case Action::Next:change(1,false);break;
   case Action::Seek:if(state.seekable&&state.duration>0)player.Seek(state.duration*hit.fraction);break;
   case Action::VolumeDown:volume(-5);break;case Action::VolumeUp:volume(5);break;
   case Action::Mode:settings.mode=static_cast<Mode>((int(settings.mode)+1)%4);model.SetMode(settings.mode);break;
   case Action::Search:{auto text=Keyboard("搜索歌曲、歌手、专辑",model.query);model.query=text;resetSelection();break;}
   case Action::Back:if(!model.query.empty())model.query.clear();else model.groupKey.clear();resetSelection();break;
   case Action::Enqueue:if(!model.ShowingGroups())if(auto t=chosen()){model.Enqueue(*t);notice="已加入播放队列";}break;
   case Action::Favorite:{const Track*t=page==Page::Library?(!model.ShowingGroups()?chosen():nullptr):model.Current();if(t){model.ToggleFavorite(*t);save();}}break;
   case Action::Tape:tape=!tape;break;case Action::Refresh:thumbnails.Clear(true);if(auto t=model.Current())artwork.Request(*t,true);startScan();break;
   case Action::SourceAdd:sourceEditor(-1);break;case Action::SourceEdit:sourceEditor(hit.index);break;
   case Action::SourceDelete:if(hit.index>=0&&size_t(hit.index)<model.user.sources.size()){model.user.sources.erase(model.user.sources.begin()+hit.index);std::string e;if(!SaveSources(ConfigRoot+"/sources.json",model.user.sources,&e))notice=e;else {startScan();save();}}break;
   case Action::CacheDown:case Action::CacheUp:{int64_t value=int64_t(cache.Limit())+(hit.action==Action::CacheUp?64ll:-64ll)*1024*1024;cache.SetLimit(std::max<int64_t>(64ll*1024*1024,std::min<int64_t>(4096ll*1024*1024,value)));cache.Prune(pins());WritePlaylist(ConfigRoot+"/cache-limit.txt",std::to_string(cache.Limit()));break;}
   case Action::ClearCache:cache.ClearIdle(pins());notice="已清理闲置缓存，当前播放缓存已保留";break;
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
   }rebuild();
  };
  PadState pad{};padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);hidInitializeTouchScreen();
  u64 lastFrame=svcGetSystemTick(),lastRepeat=0,lastSave=lastFrame,lastUsage=0;uint64_t handledEnd=0,cachedUsage=0;double animation=0;
  bool touching=false;int touchX=0,touchY=0,lastX=0,lastY=0;
  while(appletMainLoop()){
   u64 begin=svcGetSystemTick();double dt=std::min(.1,armTicksToNs(begin-lastFrame)/1e9);lastFrame=begin;
   if(scanning&&scanDone.load(std::memory_order_acquire)){scanner.join();scanning=false;model.SetTracks(SourceKind::SD,std::move(pending.sd));model.SetTracks(SourceKind::Cloud,std::move(pending.cloud));stale=pending.stale;notice=pending.notice;rebuild();if(refreshAgain){refreshAgain=false;startScan();}}
   auto state=player.Snapshot();
   if(state.ended&&state.completion!=handledEnd){handledEnd=state.completion;change(1,true);state=player.Snapshot();}
   if(auto t=model.Current())if(artKey!=t->id){artKey=t->id;artwork.Request(*t);lyricScroll=-1;}
   auto art=artwork.Snapshot();if(!model.Current()||art.trackId!=model.Current()->id)art={};
   if(lyricScroll>=0&&armTicksToNs(begin-lastLyricManual)>=6000000000ull)lyricScroll=-1;
   auto scrollLyrics=[&](int rows){double userOffset=model.Current()?model.user.lyricsOffsets[model.Current()->id]:0;
    int first=lyricScroll>=0?lyricScroll:ClampLyricScroll(art.lyrics.get(),art.lyrics?art.lyrics->Current(state.seconds,userOffset)-2:0);
    lyricScroll=ClampLyricScroll(art.lyrics.get(),first+rows);lastLyricManual=begin;
   };
   padUpdate(&pad);u64 down=padGetButtonsDown(&pad),held=padGetButtons(&pad);if(down&HidNpadButton_Plus)break;
   u64 nav=down;if(down&(HidNpadButton_Up|HidNpadButton_Down))lastRepeat=begin+armNsToTicks(250000000);
   else if(begin>lastRepeat){nav|=held&(HidNpadButton_Up|HidNpadButton_Down);lastRepeat=begin+armNsToTicks(100000000);}
   if(down&HidNpadButton_Minus){page=static_cast<Page>((int(page)+1)%5);resetSelection();}
   if(nav&HidNpadButton_Up){if(page==Page::Lyrics)scrollLyrics(-1);else --selected;reveal();}
   if(nav&HidNpadButton_Down){if(page==Page::Lyrics)scrollLyrics(1);else ++selected;reveal();}
   if(down&HidNpadButton_Left)volume(-5);
   if(down&HidNpadButton_Right)volume(5);
   if(down&HidNpadButton_A){if(page==Page::Library)act({model.ShowingGroups()?Action::GroupItem:Action::Song,selected});else if(page==Page::Queue)act({Action::QueueSong,selected});else if(page==Page::Settings)act({Action::SourceEdit,selected});else if(page==Page::Player&&chosen()&&(!model.Current()||chosen()->id!=model.Current()->id))act({Action::Song,selected});else act({Action::Toggle});}
   if(down&HidNpadButton_B){if(page==Page::Lyrics)lyricScroll=-1;else if(page==Page::Library)act({Action::Back});else {page=Page::Library;resetSelection();}}
   if(down&HidNpadButton_X){if(page==Page::Library)act({Action::Enqueue});else if(page==Page::Queue)act({Action::QueueRemove,selected});else if(page==Page::Settings)act({Action::SourceAdd});else startScan();}
   if(down&HidNpadButton_Y)act({page==Page::Library?(model.source==LibrarySource::Playlists?Action::DeletePlaylist:Action::Favorite):Action::Mode,selected});
   if(down&HidNpadButton_L)change(-1,false);
   if(down&HidNpadButton_R)change(1,false);
   if(state.seekable&&down&HidNpadButton_ZL)player.Seek(std::max(0.0,state.seconds-10));
   if(state.seekable&&down&HidNpadButton_ZR)player.Seek(state.durationKnown?std::min(state.duration,state.seconds+10):state.seconds+10);
   View view;view.page=page;view.tracks=&visible;view.groups=&groups;view.sources=&model.user.sources;view.playing=model.Current();view.current=-1;
   if(view.playing)for(size_t i=0;i<visible.size();++i)if(visible[i].id==view.playing->id){view.current=i;break;}
   view.selected=selected;view.offset=offset;view.volume=settings.volume;view.mode=settings.mode;view.librarySource=model.source;view.grouping=model.grouping;view.showingGroups=page==Page::Library&&model.ShowingGroups();view.query=model.query;view.subtitle=model.groupKey;
   view.paused=state.paused;view.loading=state.loading;view.scanning=scanning;view.seconds=state.seconds;view.duration=state.duration;view.error=state.error;view.ended=state.ended;view.sourceRate=state.rate;view.seekable=state.seekable;view.durationKnown=state.durationKnown;view.buffering=state.buffering;
   view.tape=tape;view.stale=stale;view.notice=notice.empty()?art.note:notice;view.resumeAvailable=model.user.resume.valid&&state.path.empty();view.favorite=view.playing&&model.Favorite(*view.playing);
   view.coverImage=art.cover.get();view.lyricData=art.lyrics.get();view.lyricScroll=lyricScroll;if(view.playing)view.lyricOffset=model.user.lyricsOffsets[view.playing->id];
   if(armTicksToNs(begin-lastUsage)>1000000000ull){cachedUsage=cache.Usage().bytes;lastUsage=begin;}
   view.cacheBytes=cachedUsage;view.cacheLimit=cache.Limit();auto network=player.StreamSnapshotValue();view.networkBytes=network.downloadedBytes;view.networkTotal=network.totalBytes;
   if(page==Page::Library||page==Page::Queue)for(int row=0;row<VisibleRows;++row){int i=offset+row;const Track*track=nullptr;
    if(view.showingGroups&&i>=0&&size_t(i)<groups.size()){
     const auto&key=groups[i].key;if(model.source==LibrarySource::Playlists){for(const auto&p:model.user.playlists)if(p.id==key&&!p.tracks.empty()){track=&p.tracks.front();break;}}
     else {auto split=key.find('\n');for(const auto&t:visible){bool match=model.grouping==Grouping::Albums?split!=std::string::npos&&key.compare(0,split,t.albumArtist.empty()?t.artist:t.albumArtist)==0&&key.compare(split+1,std::string::npos,t.album)==0:t.artist==key;if(match){track=&t;break;}}}
    }else if(i>=0&&size_t(i)<visible.size())track=&visible[i];
    view.thumbnails.push_back(track?thumbnails.Get(*track):nullptr);
   }
   if(!state.paused&&!state.ended&&!state.loading&&!state.buffering&&state.error.empty())animation=std::fmod(animation+dt*.9,6.283185307);
   view.animation=animation;
   HidTouchScreenState touch{};bool hasTouch=hidGetTouchScreenStates(&touch,1)>0&&touch.count>0;
   if(hasTouch){lastX=touch.touches[0].x;lastY=touch.touches[0].y;if(!touching){touching=true;touchX=lastX;touchY=lastY;}}
   else if(touching){touching=false;auto gesture=ResolveGesture(touchX,touchY,lastX,lastY,view);
    if(gesture.scrolling){if(gesture.lyrics)scrollLyrics(gesture.scrollRows);else {int rows=page==Page::Settings?4:VisibleRows;offset=std::max(0,std::min(offset+gesture.scrollRows,std::max(0,count()-rows)));selected=offset;}}
    else act(gesture.hit);
    continue; // Actions can replace model storage referenced by this frame.
   }
   u32 stride=0;auto pixels=static_cast<uint32_t*>(framebufferBegin(&fb,&stride));renderer.Draw(pixels,stride,view);framebufferEnd(&fb);
   if(armTicksToNs(begin-lastSave)>15000000000ull){save(false);lastSave=begin;}
   u64 elapsed=armTicksToNs(svcGetSystemTick()-begin);if(elapsed<33333333)svcSleepThread(33333333-elapsed);
  }
  ++scanGeneration;if(scanner.joinable())scanner.join();save();player.Stop();appletSetAutoSleepDisabled(false);
 }
 framebufferClose(&fb);if(plReady)plExit();if(sockets)socketExit();return 0;
}
