#include "ui.hpp"
#include <cstdio>
#include <limits>
#include <vector>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL UI line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    std::vector<Track> songs(12,Track{"/music/example.mp3","很长的中文曲名，用于验证边界与触摸","中文名字"});
    View v;v.tracks=&songs;v.duration=120;v.current=1;v.selected=3;
    // Geometry is the contract used by drawing, taps and drags for all three themes.
    for(auto theme:{Theme::AlbumLiner,Theme::NightRecord,Theme::LyricTheatre}){
        v.theme=theme;v.duration=120;v.seekable=true;auto l=GetPlayerLayout(theme);
        CHECK(l.libraryTab.height>=64&&l.playerTab.height>=64&&l.settingsTab.height>=64);
        auto center=[&](const Rect&r){return HitTest(r.x+r.width/2,r.y+r.height/2,v);};
        CHECK(center(l.libraryTab).action==Action::PageLibrary);CHECK(center(l.playerTab).action==Action::PagePlayer);
        CHECK(center(l.settingsTab).action==Action::PageSettings);CHECK(center(l.toggle).action==Action::Toggle);
        CHECK(center(l.toggle).index==1);CHECK(center(l.previous).action==Action::Previous);CHECK(center(l.next).action==Action::Next);
        CHECK(center(l.lyrics).action==Action::PageLyrics);CHECK(center(l.queue).action==Action::PageQueue);
        CHECK(center(l.favorite).action==Action::Favorite);CHECK(center(l.cover).action==Action::Tape);
        CHECK(HitTest(l.seek.x,l.seek.y,v).fraction==0);
        CHECK(HitTest(l.seek.x+l.seek.width-1,l.seek.y+l.seek.height-1,v).fraction==1);
        CHECK(HitTest(l.seek.x+l.seek.width,l.seek.y,v).action==Action::None);
        auto drag=ResolveGesture(l.seek.x,l.seek.y,-100,l.seek.y,v);CHECK(drag.hit.action==Action::Seek&&drag.hit.fraction==0);
        drag=ResolveGesture(l.seek.x,l.seek.y,2000,l.seek.y,v);CHECK(drag.hit.action==Action::Seek&&drag.hit.fraction==1);
        const int mid=l.seek.x+(l.seek.width-1)/2;
        CHECK(center(l.seek).action==Action::Seek);
        CHECK(ResolveGesture(mid,l.seek.y,mid,l.seek.y+100,v).hit.fraction==l.SeekFraction(mid));
        v.seekable=false;CHECK(center(l.seek).action==Action::None);
        v.seekable=true;v.duration=std::numeric_limits<double>::infinity();CHECK(center(l.seek).action==Action::None);
        v.duration=0;CHECK(center(l.seek).action==Action::None);
        // Progress, cover, and transport must never steal each other's presses.
        for(const auto&r:{l.cover,l.toggle,l.previous,l.next,l.lyrics}){
            CHECK(!l.seek.Contains(r.x+r.width/2,r.y+r.height/2));
        }
    }
    CHECK(GetPlayerLayout(static_cast<Theme>(999)).cover.width==GetPlayerLayout(Theme::AlbumLiner).cover.width);
    CHECK(HitTest(-20,10,v).action==Action::None);CHECK(HitTest(1290,10,v).action==Action::None);
    // All pages keep the same header geometry, with no obsolete second tab bar.
    for(auto page:{Page::Library,Page::Player,Page::Lyrics,Page::Queue,Page::Settings}){
        v.page=page;auto l=GetPlayerLayout(v.theme);
        CHECK(HitTest(l.libraryTab.x+12,l.libraryTab.y+12,v).action==Action::PageLibrary);
        CHECK(HitTest(l.playerTab.x+12,l.playerTab.y+12,v).action==Action::PagePlayer);
        CHECK(HitTest(l.settingsTab.x+12,l.settingsTab.y+12,v).action==Action::PageSettings);
    }
    v.page=Page::Library;v.duration=120;
    CHECK(HitTest(80,220,v).action==Action::SourceSD);
    CHECK(HitTest(80,292,v).action==Action::SourceCloud);
    CHECK(HitTest(80,364,v).action==Action::SourceFavorites);
    CHECK(HitTest(80,436,v).action==Action::SourcePlaylists);
    CHECK(HitTest(280,205,v).action==Action::GroupSongs);
    CHECK(HitTest(390,205,v).action==Action::GroupAlbums);
    CHECK(HitTest(500,205,v).action==Action::GroupArtists);
    CHECK(HitTest(750,205,v).action==Action::Search);
    CHECK(HitTest(300,270,v).action==Action::Song);CHECK(HitTest(300,270,v).index==0);
    CHECK(HitTest(300,350,v).index==1);CHECK(HitTest(300,318,v).action==Action::None);
    v.offset=3;CHECK(HitTest(300,270,v).index==3);
    auto swipe=ResolveGesture(800,430,800,260,v);CHECK(swipe.scrolling&&swipe.scrollRows==2);
    auto ignored=ResolveGesture(100,300,100,150,v);CHECK(!ignored.scrolling&&ignored.hit.action==Action::None);
    std::vector<LibraryGroup>groups(6,LibraryGroup{"album","专辑","歌手",1});v.groups=&groups;v.showingGroups=true;v.offset=0;
    CHECK(HitTest(300,270,v).action==Action::GroupItem);CHECK(HitTest(800,270,v).index==1);
    CHECK(HitTest(300,440,v).index==2);CHECK(HitTest(740,270,v).action==Action::None);
    v.librarySource=LibrarySource::Playlists;
    CHECK(HitTest(1150,150,v).action==Action::DeletePlaylist);
    v.showingGroups=false;CHECK(HitTest(1150,150,v).action==Action::Enqueue);
    v.librarySource=LibrarySource::SD;
    v.page=Page::Queue;
    CHECK(HitTest(70,150,v).action==Action::PagePlayer);
    CHECK(HitTest(1150,180,v).action==Action::QueueRemove);CHECK(HitTest(1150,180,v).index==3);
    CHECK(HitTest(750,180,v).action==Action::QueueSong);CHECK(HitTest(750,180,v).index==3);
    CHECK(HitTest(900,180,v).action==Action::QueueUp);CHECK(HitTest(1020,180,v).action==Action::QueueDown);
    v.page=Page::Lyrics;CHECK(HitTest(70,150,v).action==Action::PagePlayer);CHECK(HitTest(850,170,v).action==Action::LyricEarlier);
    auto lyrics=ResolveGesture(800,460,800,300,v);CHECK(lyrics.scrolling&&lyrics.lyrics&&lyrics.scrollRows==2);
    auto wrapped=WrapPlainLyrics("一二三四\nabcde",6);CHECK(wrapped.size()==3);CHECK(wrapped[0]=="一二三");CHECK(wrapped[1]=="四");
    Lyrics plain;plain.plain="一\n二\n三";CHECK(ClampLyricScroll(&plain,99)==2);CHECK(ClampLyricScroll(&plain,-9)==0);
    v.page=Page::Settings;v.offset=0;v.scrollPixels=0;
    CHECK(HitTest(100,240,v).action==Action::SettingsPlayback);
    CHECK(HitTest(100,320,v).action==Action::SettingsSources);
    CHECK(HitTest(100,400,v).action==Action::SettingsStorage);
    CHECK(HitTest(100,480,v).action==Action::SettingsPlaylists);
    // Each controller selection has an identical reachable touch action.
    struct SettingTouch {SettingsSection section;int selected,x,y;Action action;int index;};
    const SettingTouch settings[]={
        {SettingsSection::Playback,0,970,250,Action::VolumeDown,-1},
        {SettingsSection::Playback,1,1160,250,Action::VolumeUp,-1},
        {SettingsSection::Playback,2,600,340,Action::Mode,-1},
        {SettingsSection::Playback,3,450,470,Action::CycleTheme,0},
        {SettingsSection::Playback,4,750,470,Action::CycleTheme,1},
        {SettingsSection::Playback,5,1050,470,Action::CycleTheme,2},
        {SettingsSection::Storage,0,970,250,Action::CacheDown,-1},
        {SettingsSection::Storage,1,1160,250,Action::CacheUp,-1},
        {SettingsSection::Storage,2,600,340,Action::ClearCache,-1},
        {SettingsSection::Storage,3,500,440,Action::Refresh,-1},
        {SettingsSection::Playlists,0,600,260,Action::ImportPlaylist,-1},
        {SettingsSection::Playlists,1,600,360,Action::SavePlaylist,-1},
        {SettingsSection::Playlists,2,600,460,Action::ExportPlaylist,-1}
    };
    for(const auto&setting:settings){v.settingsSection=setting.section;v.selected=setting.selected;
        auto hit=HitTest(setting.x,setting.y,v),confirm=SettingAction(v);
        CHECK(hit.action==setting.action&&hit.index==setting.index);CHECK(confirm.action==hit.action&&confirm.index==hit.index);
        CHECK(!ResolveGesture(800,420,800,260,v).scrolling);
    }
    CHECK(SettingCount(SettingsSection::Playback)==6);CHECK(SettingCount(SettingsSection::Storage)==4);CHECK(SettingCount(SettingsSection::Playlists)==3);
    v.selected=99;CHECK(SettingAction(v).action==Action::None);
    std::vector<MediaSource>sources(8);v.sources=&sources;v.settingsSection=SettingsSection::Sources;v.selected=4;v.offset=3;
    CHECK(HitTest(970,180,v).action==Action::SourceAdd);
    CHECK(HitTest(1150,180,v).action==Action::Refresh&&HitTest(1150,180,v).index==-1);
    CHECK(HitTest(500,270,v).action==Action::SourceEdit&&HitTest(500,270,v).index==3);
    CHECK(HitTest(500,350,v).index==4);CHECK(HitTest(500,318,v).action==Action::None);
    CHECK(HitTest(400,530,v).action==Action::SourceEdit&&HitTest(400,530,v).index==4);
    CHECK(HitTest(1120,530,v).action==Action::SourceDelete&&HitTest(1120,530,v).index==4);
    CHECK(SettingAction(v).action==Action::SourceEdit&&SettingAction(v).index==4);
    CHECK(ResolveGesture(500,440,500,270,v).scrolling);CHECK(!ResolveGesture(100,440,100,270,v).scrolling);
    v.scrollPixels=24;CHECK(HitTest(500,242,v).index==2);CHECK(HitTest(500,270,v).index==3);
    v.scrollPixels=0;v.offset=0;sources.clear();CHECK(SettingAction(v).action==Action::SourceAdd);
    CHECK(HitTest(1120,530,v).action==Action::None);
    for(auto page:{Page::Library,Page::Lyrics,Page::Queue,Page::Settings}){v.page=page;
        CHECK(HitTest(924,640,v).action==Action::Previous);CHECK(HitTest(1004,640,v).action==Action::Toggle);
        CHECK(HitTest(1084,640,v).action==Action::Next);CHECK(HitTest(1204,640,v).action==Action::PageQueue);
    }
    CHECK(std::string(ThemeName(Theme::AlbumLiner))!="");
    CHECK(std::string(ThemeName(Theme::AlbumLiner))!=ThemeName(Theme::NightRecord));
    std::vector<uint32_t>pixels(1280*720+32,0xdeadbeef);Renderer r;
    CoverImage art;art.width=art.height=2;art.pixels.assign(4,0xff5a3928);v.coverImage=&art;
    Lyrics longLyrics;longLyrics.plain="很长的中文歌词，应该在有限空间换行，而不会覆盖进度条或按钮。\n下一行";v.lyricData=&longLyrics;
    uint32_t backgrounds[3]{};
    for(int pass=0;pass<2;++pass)for(auto theme:{Theme::AlbumLiner,Theme::NightRecord,Theme::LyricTheatre}){
        v.theme=theme;
        for(auto page:{Page::Library,Page::Player,Page::Lyrics,Page::Queue,Page::Settings}){
            v.page=page;r.Draw(pixels.data()+16,1280*4,v);
            for(int i=0;i<16;++i){CHECK(pixels[i]==0xdeadbeef);CHECK(pixels[pixels.size()-1-i]==0xdeadbeef);}
            if(page==Page::Settings)for(auto section:{SettingsSection::Playback,SettingsSection::Sources,SettingsSection::Storage,SettingsSection::Playlists}){
                v.settingsSection=section;r.Draw(pixels.data()+16,1280*4,v);
                for(int i=0;i<16;++i){CHECK(pixels[i]==0xdeadbeef);CHECK(pixels[pixels.size()-1-i]==0xdeadbeef);}
            }
            if(page==Page::Player){auto l=GetPlayerLayout(theme);backgrounds[int(theme)]=pixels[16];
                CHECK(pixels[16+(l.cover.y+l.cover.height/2)*1280+l.cover.x+l.cover.width/2]==art.pixels[0]);
            }
        }
        // Same source object can receive replacement artwork between theme switches.
        art.pixels.assign(4,pass?0xff592a86:0xff95834a);
        v.page=Page::Player;r.Draw(pixels.data()+16,1280*4,v);auto l=GetPlayerLayout(theme);
        CHECK(pixels[16+(l.cover.y+l.cover.height/2)*1280+l.cover.x+l.cover.width/2]==art.pixels[0]);
        v.coverImage=nullptr;v.lyricData=nullptr;v.artLoading=true;v.error="无法打开音乐，试试下一首";
        r.Draw(pixels.data()+16,1280*4,v);v.artLoading=false;v.error.clear();v.coverImage=&art;v.lyricData=&longLyrics;
    }
    CHECK(backgrounds[0]!=backgrounds[1]&&backgrounds[1]!=backgrounds[2]&&backgrounds[0]!=backgrounds[2]);
    songs.clear();v.page=Page::Library;CHECK(HitTest(300,270,v).action==Action::None);
    std::puts("PASS UI: unified navigation, settings controller/touch parity, source and group geometry, playlist safety, three-theme rendering");
}
