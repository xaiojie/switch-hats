#include "ui.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
using namespace papa;
int main(int argc,char**argv){
    if(argc!=3){std::puts("preview FONT_FILE OUTPUT_DIRECTORY");return 1;}
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char>font((std::istreambuf_iterator<char>(file)),{});
    Renderer renderer;if(!renderer.AddFont(font.data(),font.size()))return 1;
    MediaCache cache("build-host/preview-cache");
    auto image=DecodeCover(cache.ReadAsset("assets/icon-master.png",4*1024*1024));
    std::vector<Track>songs;
    const char*titles[]={"演示曲目 01","演示曲目 02","演示曲目 03","演示曲目 04","演示曲目 05","演示曲目 06"};
    for(int i=0;i<6;++i){Track t{"sdmc:/music/demo"+std::to_string(i)+".mp3",titles[i],"示例歌手"};t.id="sd:demo"+std::to_string(i);t.album=i<3?"示例专辑 A":"示例专辑 B";t.duration=240+i*10;songs.push_back(t);}
    Track remote{"https://music.example.test/audio/demo.mp3","云端演示曲目","示例歌手"};remote.id="cloud:demo:01";remote.source=SourceKind::Cloud;remote.album="云端示例专辑";remote.duration=270;
    LibraryModel model;model.SetTracks(SourceKind::SD,songs);model.SetTracks(SourceKind::Cloud,{remote});model.StartVisible(0);model.Enqueue(remote);
    model.ToggleFavorite(songs[0]);model.SaveQueueAsPlaylist("我的示例歌单");
    std::vector<MediaSource>sources{{"demo","我的电脑音乐库","http://music.example.test/library.json",true}};
    Lyrics lyrics=ParseLyrics("[00:10.00]这是一份界面演示歌词\n[00:30.00]封面、专辑与歌词同屏\n[01:00.00]支持 SD 卡和云端音乐库\n[01:20.00]正在播放的这一行会高亮\n[01:40.00]暂停、切歌后继续保持同步\n[02:10.00]示例文字并非真实歌曲歌词");
    View view;view.playing=model.Current();view.current=0;view.selected=0;view.paused=false;view.seconds=83;view.duration=240;view.durationKnown=true;view.sourceRate=44100;view.animation=.37;view.volume=70;
    view.coverImage=image.get();view.lyricData=&lyrics;view.sources=&sources;view.favorite=true;view.resumeAvailable=true;view.thumbnails.assign(6,image);
    std::vector<Track>visible;std::vector<LibraryGroup>groups;std::vector<uint32_t>pixels(ScreenWidth*ScreenHeight);
    auto save=[&](const char*name){renderer.Draw(pixels.data(),ScreenWidth*4,view);std::string path=std::string(argv[2])+"/"+name;return stbi_write_png(path.c_str(),ScreenWidth,ScreenHeight,4,pixels.data(),ScreenWidth*4)!=0;};
    view.page=Page::Player;view.tape=false;view.tracks=&model.Queued();if(!save("player-cover.png"))return 1;
    view.tape=true;if(!save("player-tape.png"))return 1;
    view.page=Page::Lyrics;if(!save("lyrics.png"))return 1;
    view.page=Page::Library;view.librarySource=LibrarySource::SD;view.tracks=&songs;view.lyricData=nullptr;if(!save("library-sd.png"))return 1;
    model.SelectSource(LibrarySource::Cloud);visible=model.Visible();view.librarySource=LibrarySource::Cloud;view.tracks=&visible;if(!save("library-cloud.png"))return 1;
    model.SelectSource(LibrarySource::SD);model.SelectGrouping(Grouping::Albums);groups=model.Groups();view.librarySource=LibrarySource::SD;view.grouping=Grouping::Albums;view.groups=&groups;view.showingGroups=true;if(!save("albums.png"))return 1;
    view.page=Page::Queue;view.showingGroups=false;view.tracks=&model.Queued();if(!save("queue.png"))return 1;
    view.page=Page::Settings;view.cacheBytes=34*1024*1024;if(!save("settings.png"))return 1;
    view.page=Page::Player;view.tape=false;view.buffering=true;view.notice="示例：网络暂时没有数据，正在缓冲";if(!save("buffering.png"))return 1;
    std::puts("Rendered actual 0.3 UI: SD/cloud, albums, cover/tape, lyrics, queue, settings, buffering. Demonstration metadata only.");
}
