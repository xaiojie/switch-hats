#include "ui.hpp"
#include <cstdio>
#include <vector>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL UI line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    auto wrapped=WrapPlainLyrics("一二三四\nabcde",6);CHECK(wrapped.size()==3);CHECK(wrapped[0]=="一二三");CHECK(wrapped[1]=="四");
    Lyrics plain;plain.plain="一\n二\n三";CHECK(ClampLyricScroll(&plain,99)==2);CHECK(ClampLyricScroll(&plain,-9)==0);
    std::vector<Track> songs(9,Track{"/music/example.mp3",std::string(400,'A'),"中文名字"});
    View v;v.tracks=&songs;v.duration=120;
    CHECK(HitTest(410,598,v).action==Action::Toggle);
    v.duration=120;auto gesture=ResolveGesture(400,523,740,523,v);CHECK(gesture.hit.action==Action::Seek);CHECK(gesture.hit.fraction>.9);
    v.current=1;v.selected=3;CHECK(HitTest(410,598,v).index==1);
    CHECK(HitTest(304,598,v).action==Action::Previous);
    CHECK(HitTest(506,598,v).action==Action::Next);
    CHECK(HitTest(860,230,v).index==0);CHECK(HitTest(860,294,v).index==1);
    v.offset=3;CHECK(HitTest(860,230,v).index==3);
    auto seek=HitTest(404,523,v);CHECK(seek.action==Action::Seek);CHECK(seek.fraction>0.49&&seek.fraction<0.51);
    v.duration=0;CHECK(HitTest(404,523,v).action==Action::None);
    CHECK(HitTest(-20,10,v).action==Action::None);CHECK(HitTest(1290,10,v).action==Action::None);
    std::vector<uint32_t> pixels(1280*720+32,0xdeadbeef);
    Renderer r;r.Draw(pixels.data()+16,1280*4,v);
    for(int i=0;i<16;++i){CHECK(pixels[i]==0xdeadbeef);CHECK(pixels[pixels.size()-1-i]==0xdeadbeef);}
    songs.clear();CHECK(HitTest(860,230,v).action==Action::None);
    r.Draw(pixels.data()+16,1280*4,v);
    for(int i=0;i<16;++i){CHECK(pixels[i]==0xdeadbeef);CHECK(pixels[pixels.size()-1-i]==0xdeadbeef);}
    v.page=Page::Library;
    CHECK(HitTest(201,110,v).action==Action::PagePlayer);
    CHECK(HitTest(200,160,v).action==Action::SourceCloud);
    CHECK(HitTest(970,160,v).action==Action::Search);
    std::vector<LibraryGroup>groups{{"album","专辑","歌手",1}};v.groups=&groups;v.showingGroups=true;v.offset=0;
    CHECK(HitTest(100,250,v).action==Action::GroupItem);
    v.page=Page::Queue;v.showingGroups=false;songs.resize(1);
    CHECK(HitTest(1190,230,v).action==Action::QueueRemove);
    v.page=Page::Lyrics;CHECK(HitTest(880,160,v).action==Action::LyricEarlier);
    v.duration=120;auto mini=ResolveGesture(900,646,1200,646,v);CHECK(mini.hit.action==Action::Seek);
    auto swipe=ResolveGesture(800,400,800,280,v);CHECK(swipe.scrolling&&swipe.lyrics&&swipe.scrollRows==2);
    auto ignored=ResolveGesture(100,110,300,110,v);CHECK(!ignored.scrolling&&ignored.hit.action==Action::None);
    v.page=Page::Settings;CHECK(HitTest(1120,160,v).action==Action::SourceAdd);
    for(auto page:{Page::Library,Page::Player,Page::Lyrics,Page::Queue,Page::Settings}){
        v.page=page;r.Draw(pixels.data()+16,1280*4,v);
        for(int i=0;i<16;++i){CHECK(pixels[i]==0xdeadbeef);CHECK(pixels[pixels.size()-1-i]==0xdeadbeef);}
    }
    std::puts("PASS UI: touch targets, scrolling, seek bounds, empty library, render guards");
}
