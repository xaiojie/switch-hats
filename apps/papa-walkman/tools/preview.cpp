#include "ui.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
using namespace papa;
int main(int argc,char**argv){
    if(argc<3||argc>4){std::puts("preview FONT_FILE OUTPUT_DIRECTORY [COVER_FILE]");return 1;}
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char>font((std::istreambuf_iterator<char>(file)),{});
    Renderer renderer;if(!renderer.AddFont(font.data(),font.size()))return 1;
    if(!CreateDirectories(argv[2]))return 1;
    MediaCache cache("build-host/preview-cache");
    const std::string coverPath=argc==4?argv[3]:"docs/design/2026-10-30-player-redesign/demo-cover.png";
    auto image=DecodeCover(cache.ReadAsset(coverPath,16*1024*1024));
    if(!image)image=DecodeCover(cache.ReadAsset("assets/icon-master.png",4*1024*1024));
    std::vector<Track>songs;
    const char*titles[]={"夜间散步","把音乐带上","日落之前","今天的风","沿着河岸","明天见"};
    for(int i=0;i<6;++i){Track t{"sdmc:/music/demo"+std::to_string(i)+".mp3",titles[i],"爬爬"};t.id="sd:demo"+std::to_string(i);t.album=i<3?"把音乐带上":"一小段日常";t.duration=222+i*10;songs.push_back(t);}
    Track remote{"https://music.example.test/audio/demo.mp3","云端演示曲目","示例歌手"};remote.id="cloud:demo:01";remote.source=SourceKind::Cloud;remote.album="云端示例专辑";remote.duration=270;
    LibraryModel model;model.SetTracks(SourceKind::SD,songs);model.SetTracks(SourceKind::Cloud,{remote});model.StartVisible(0);model.Enqueue(remote);
    std::vector<MediaSource>sources{{"demo","我的电脑音乐库","http://music.example.test/library.json",true},{"demo2","日常随听","http://music.example.test/music.m3u",true},{"demo3","喜欢的专辑","http://music.example.test/albums.m3u",false}};
    // Original demonstration copy, shared by every theme for a fair comparison.
    Lyrics lyrics=ParseLyrics("[00:10.00]路灯为晚风留了位置\n[00:30.00]城市安静下来\n[01:20.00]走慢一点，让音乐先到\n[01:40.00]今晚的风刚刚好\n[02:10.00]把这一刻装进口袋");
    Lyrics plain;plain.plain="没有时间标签的长歌词也可以分行显示。这里使用较长的演示文本，检查中文自动换行与连续滚动。\n每一段音乐，都陪你走过一小段日常。\n这些是原创演示文字。";
    Track longTrack=songs.front();longTrack.title="很长很长的中文歌曲名称，也应该留在自己的区域里面，不遮住任何按钮";longTrack.artist="同样很长的演唱者名称 · 界面边界演示";longTrack.album="一份用来检查文字边界的示例专辑";
    Lyrics longLyrics=ParseLyrics("[00:00]很长的一句歌词，要保证换行之后仍然能看到播放进度和操作按钮，也不会从屏幕右边溢出去\n[01:40]下一句也会保持自己的位置");
    std::vector<LibraryGroup>groups;std::vector<uint32_t>pixels(ScreenWidth*ScreenHeight);
    for(auto theme:{Theme::AlbumLiner,Theme::NightRecord,Theme::LyricTheatre}){
        View view;view.theme=theme;view.playing=model.Current();view.current=0;view.selected=0;view.paused=false;view.seconds=83;view.duration=222;view.sourceRate=44100;view.volume=70;
        view.coverImage=image.get();view.lyricData=&lyrics;view.sources=&sources;view.thumbnails.assign(6,image);view.tracks=&model.Queued();
        auto save=[&](const std::string&name){renderer.Draw(pixels.data(),ScreenWidth*4,view);std::string path=std::string(argv[2])+"/theme-"+std::to_string(int(theme))+"-"+name+".png";return stbi_write_png(path.c_str(),ScreenWidth,ScreenHeight,4,pixels.data(),ScreenWidth*4)!=0;};
        if(!save("player"))return 1;
        auto layout=GetPlayerLayout(theme);view.touchX=layout.toggle.x+layout.toggle.width/2;view.touchY=layout.toggle.y+layout.toggle.height/2;
        if(!save("pressed"))return 1;view.touchX=view.touchY=-1;
        view.seeking=true;view.seekFraction=.73;if(!save("seeking"))return 1;view.seeking=false;
        view.tape=true;if(!save("tape"))return 1;view.tape=false;
        view.lyricData=&plain;if(!save("player-plain"))return 1;view.lyricData=&lyrics;
        view.playing=&longTrack;view.lyricData=&longLyrics;if(!save("long-text"))return 1;view.playing=model.Current();view.lyricData=&lyrics;
        view.page=Page::Lyrics;if(!save("lyrics"))return 1;view.lyricData=&plain;if(!save("lyrics-plain"))return 1;view.lyricData=&lyrics;
        view.page=Page::Library;view.tracks=&songs;if(!save("library"))return 1;
        view.offset=1;view.scrollPixels=32;if(!save("library-scrolling"))return 1;view.offset=view.scrollPixels=0;
        model.SelectSource(LibrarySource::SD);model.SelectGrouping(Grouping::Albums);groups=model.Groups();view.grouping=Grouping::Albums;view.groups=&groups;view.showingGroups=true;if(!save("albums"))return 1;view.showingGroups=false;
        view.page=Page::Queue;view.tracks=&model.Queued();view.selected=2;if(!save("queue"))return 1;
        view.page=Page::Settings;view.selected=2;view.cacheBytes=34*1024*1024;if(!save("settings"))return 1;
        view.settingsSection=SettingsSection::Sources;view.selected=0;if(!save("settings-sources"))return 1;
        view.settingsSection=SettingsSection::Storage;view.selected=2;if(!save("settings-storage"))return 1;
        view.settingsSection=SettingsSection::Playlists;view.selected=1;if(!save("settings-playlists"))return 1;
        view.settingsSection=SettingsSection::Playback;view.selected=3;
        view.error="连接失败，请检查音乐源地址后重试";if(!save("settings-error"))return 1;view.error.clear();
        view.page=Page::Player;view.playing=&remote;view.coverImage=nullptr;view.duration=0;view.durationKnown=false;view.lyricData=nullptr;
        if(!save("missing-metadata"))return 1;
        view.artLoading=true;view.buffering=true;view.notice="网络暂时没有数据，正在缓冲";if(!save("buffering"))return 1;
        view.error="无法打开这首音乐，请检查文件或切换到下一首";if(!save("error"))return 1;
        view=View{};view.theme=theme;if(!save("empty"))return 1;
        view.playing=model.Current();view.coverImage=image.get();view.lyricData=&lyrics;view.duration=222;view.seconds=83;view.paused=false;
        for(int i=0;i<8;++i)renderer.Draw(pixels.data(),ScreenWidth*4,view);
        std::vector<double> elapsed;
        for(int i=0;i<120;++i){view.seconds=83+i/60.;auto start=std::chrono::steady_clock::now();renderer.Draw(pixels.data(),ScreenWidth*4,view);elapsed.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
        std::sort(elapsed.begin(),elapsed.end());std::printf("%s native host render: median %.2f ms, p95 %.2f ms, max %.2f ms (120 cached frames; host only, not Switch FPS).\n",ThemeName(theme),elapsed[60],elapsed[114],elapsed.back());
    }
    std::puts("Rendered native unified 0.5.1 UI: three themes with all four settings sections and child pages. Original demonstration metadata; real dynamic artwork and lyric rendering.");
}
