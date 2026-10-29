#include "ui.hpp"
#include <cstdio>
#include <vector>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL UI line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    std::vector<Track> songs(9,Track{"/music/example.mp3",std::string(400,'A'),"中文名字"});
    View v;v.tracks=&songs;v.duration=120;
    CHECK(HitTest(410,598,v).action==Action::Toggle);
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
    std::puts("PASS UI: touch targets, scrolling, seek bounds, empty library, render guards");
}
