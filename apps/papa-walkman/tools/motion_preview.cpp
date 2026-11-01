#include "lyric_motion.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
using namespace papa;
int main(int argc,char**argv){
    if(argc!=3){std::puts("motion_preview FONT_FILE OUTPUT_DIRECTORY");return 1;}
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char>font((std::istreambuf_iterator<char>(file)),{});
    Renderer renderer;if(!renderer.AddFont(font.data(),font.size()))return 1;
    if(!CreateDirectories(argv[2]))return 1;
    std::ifstream coverFile("docs/design/2026-10-30-player-redesign/demo-cover.png",std::ios::binary);
    std::vector<uint8_t>bytes((std::istreambuf_iterator<char>(coverFile)),{});
    auto image=DecodeCover(bytes);
    if(!image){std::fputs("The existing demo cover could not be loaded. Run from the repository root.\n",stderr);return 1;}
    Track track{"sdmc:/music/original-demo.mp3","夜间散步","爬爬"};track.id="sd:motion-demo";track.album="把音乐带上";track.duration=222;
    // Original demonstration words; timing is intentionally short to show the
    // exact same transition in all three live native Player compositions.
    Lyrics lyrics=ParseLyrics("[00:00]路灯为晚风留了位置\n[00:01]城市安静下来\n[00:01.50]脚步跟着旋律\n[00:02]走慢一点，让音乐先到\n[00:04]今晚的风刚刚好\n[00:06]把这一刻装进口袋\n[00:08]下一站，仍有音乐");
    std::vector<uint32_t>pixels(ScreenWidth*ScreenHeight);
    for(auto theme:{Theme::AlbumLiner,Theme::NightRecord,Theme::LyricTheatre}){
        for(auto page:{Page::Player,Page::Lyrics}){
            std::string scene="theme-"+std::to_string(int(theme))+(page==Page::Player?"-player":"-lyrics");
            std::string directory=std::string(argv[2])+"/"+scene;
            if(!CreateDirectories(directory))return 1;
            std::ofstream manifest(directory+"/frames.csv");
            manifest<<"frame,wall_seconds,playback_seconds,lyric_position,active\n";
            View view;view.theme=theme;view.page=page;view.playing=&track;view.current=0;
            view.paused=false;view.duration=222;view.coverImage=image.get();view.lyricData=&lyrics;
            // Actual glyph pixels may change only inside the agreed lyric
            // viewport while the rest of the live Player remains untouched.
            Rect bounds=page==Page::Lyrics?Rect{320,224,920,352}:
                theme==Theme::AlbumLiner?Rect{612,302,628,184}:
                theme==Theme::NightRecord?Rect{978,198,262,230}:Rect{554,132,686,356};
            view.seconds=2.2;view.lyricPosition=2;
            renderer.Draw(pixels.data(),ScreenWidth*4,view);
            auto still=pixels;view.lyricPosition=2.25;
            renderer.Draw(pixels.data(),ScreenWidth*4,view);
            int changed=0;
            for(int y=0;y<ScreenHeight;++y)for(int x=0;x<ScreenWidth;++x){
                if(pixels[size_t(y)*ScreenWidth+x]==still[size_t(y)*ScreenWidth+x])continue;
                if(!bounds.Contains(x,y)){
                    std::fprintf(stderr,"Lyric motion escaped %s viewport at (%d,%d).\n",scene.c_str(),x,y);return 1;
                }
                ++changed;
            }
            if(changed<20){std::fprintf(stderr,"No visible lyric motion in %s.\n",scene.c_str());return 1;}
            LyricMotion motion;std::vector<double>durations;
            for(int frame=0;frame<=60;++frame){
                double now=frame/60.;view.seconds=1.9+now;
                auto pose=motion.Update(view,now);view.lyricPosition=pose.position;view.lyricAnimating=pose.active;
                auto start=std::chrono::steady_clock::now();
                renderer.Draw(pixels.data(),ScreenWidth*4,view);
                durations.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
                char name[40];std::snprintf(name,sizeof(name),"/frame-%03d.png",frame);
                if(!stbi_write_png((directory+name).c_str(),ScreenWidth,ScreenHeight,4,pixels.data(),ScreenWidth*4))return 1;
                manifest<<frame<<','<<now<<','<<view.seconds<<','<<pose.position<<','<<pose.active<<'\n';
            }
            std::sort(durations.begin(),durations.end());
            std::printf("%s: 61 frames at 60 fps, native host draw median %.2f ms / p95 %.2f ms (not Switch FPS).\n",scene.c_str(),durations[30],durations[57]);
        }
    }
    std::puts("Each scene contains frame-%03d.png and frames.csv; encode the PNG sequence at 60 fps to inspect the 500 ms scroll.");
}
