#include "ui.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
using namespace papa;
int main(int argc,char**argv){
    if(argc!=3){std::puts("preview FONT_FILE OUTPUT_DIRECTORY");return 1;}
    std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char>font((std::istreambuf_iterator<char>(file)),{});
    Renderer renderer;if(!renderer.AddFont(font.data(),font.size())){std::puts("Font load failed");return 1;}
    std::vector<Track> tracks{{"","晴天","周杰伦 · 叶惠美"},{"","慢慢喜欢你","莫文蔚"},{"","夏日漱石","橘子海"},{"","City of Stars","电影原声"},{"","夜空中最亮的星","逃跑计划"},{"","松弛一下","今日私人电台"},{"","晚风里的歌","本地音乐"},{"","这是一首名字很长很长的音乐，测试中文显示边界","本地音乐"}};
    View v;v.tracks=&tracks;v.current=0;v.selected=0;v.paused=false;v.seconds=83;v.duration=269;v.animation=0.37;v.sourceRate=44100;
    std::vector<uint32_t>pixels(ScreenWidth*ScreenHeight);
    auto save=[&](const char*name){renderer.Draw(pixels.data(),ScreenWidth*4,v);std::string path=std::string(argv[2])+"/"+name;return stbi_write_png(path.c_str(),ScreenWidth,ScreenHeight,4,pixels.data(),ScreenWidth*4)!=0;};
    if(!save("playing.png"))return 1;
    v.paused=true;v.selected=7;v.offset=2;if(!save("paused.png"))return 1;
    tracks.clear();v.current=-1;v.selected=0;v.offset=0;v.seconds=v.duration=0;if(!save("empty.png"))return 1;
    v.error="这首 MP3 无法播放，请换一首";if(!save("error.png"))return 1;
    std::puts("Rendered real UI code: playing, paused, empty, error (sample library)");
}
