#include <switch.h>
#include "audio.hpp"
#include "ui.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
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
int main(int,char**){
    Framebuffer fb{};Result rc=framebufferCreate(&fb,nwindowGetDefault(),ScreenWidth,ScreenHeight,PIXEL_FORMAT_RGBA_8888,2);
    if(R_FAILED(rc)){WaitExit("Display initialization failed. Press + to exit.");return 1;}
    rc=framebufferMakeLinear(&fb);
    if(R_FAILED(rc)){framebufferClose(&fb);WaitExit("Display buffer allocation failed. Press + to exit.");return 1;}
    bool plReady=R_SUCCEEDED(plInitialize(PlServiceType_User));
    {
        Renderer renderer;bool fontReady=false;
        if(plReady){
            for(auto type:{PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified,PlSharedFontType_Standard}){
                PlFontData font{};if(R_SUCCEEDED(plGetSharedFontByType(&font,type)))fontReady=renderer.AddFont(font.address,font.size)||fontReady;
            }
        }
        if(!fontReady){framebufferClose(&fb);if(plReady)plExit();WaitExit("System font unavailable. Use title takeover (hold R while starting a game), then try again. Press + to exit.");return 1;}
        appletSetAutoSleepDisabled(true);
        mkdir("sdmc:/music",0777);mkdir("sdmc:/config",0777);mkdir("sdmc:/config/papa-walkman",0777);
        Settings settings=LoadSettings(Config);AudioPlayer player;player.Volume(settings.volume);
        Queue queue(static_cast<uint32_t>(svcGetSystemTick()));queue.SetMode(settings.mode);
        std::vector<Track>tracks,pendingTracks;std::thread scanner;std::atomic<bool>scanDone{false};bool scanning=false;
        auto startScan=[&]{if(scanning)return;if(scanner.joinable())scanner.join();scanning=true;scanDone=false;scanner=std::thread([&]{pendingTracks=ScanLibrary(MusicRoot);scanDone.store(true,std::memory_order_release);});};
        startScan();
        PadState pad{};padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);hidInitializeTouchScreen();
        int selected=0,offset=0;uint64_t handledEnd=0;double animation=0;u64 lastFrame=svcGetSystemTick(),lastRepeat=0;
        bool touching=false,dragging=false;int touchX=0,touchY=0,lastTouchX=0,lastTouchY=0;
        auto reveal=[&]{selected=std::max(0,std::min(selected,static_cast<int>(tracks.size())-1));if(selected<offset)offset=selected;if(selected>=offset+VisibleRows)offset=selected-VisibleRows+1;offset=std::max(0,std::min(offset,std::max(0,static_cast<int>(tracks.size())-VisibleRows)));};
        auto play=[&](int index){if(index<0||static_cast<size_t>(index)>=tracks.size())return;queue.Select(index);selected=index;reveal();player.Play(tracks[index].path);};
        auto change=[&](int direction,bool automatic){int index=direction>0?queue.Next(automatic):queue.Previous();if(index>=0){selected=index;reveal();player.Play(tracks[index].path);}};
        auto toggle=[&](int target){auto state=player.Snapshot();if(queue.Current()<0||queue.Current()!=target||state.ended||!state.error.empty())play(target);else player.Pause(!state.paused);};
        auto mode=[&]{settings.mode=static_cast<Mode>((static_cast<int>(settings.mode)+1)%4);queue.SetMode(settings.mode);};
        auto volume=[&](int delta){settings.volume=std::max(0,std::min(100,settings.volume+delta));player.Volume(settings.volume);};
        while(appletMainLoop()){
            u64 begin=svcGetSystemTick();double dt=std::min(0.1,armTicksToNs(begin-lastFrame)/1e9);lastFrame=begin;
            if(scanDone.load(std::memory_order_acquire)&&scanning){
                scanner.join();scanning=false;auto snapshot=player.Snapshot();int current=-1;
                for(size_t i=0;i<pendingTracks.size();++i)if(pendingTracks[i].path==snapshot.path){current=static_cast<int>(i);break;}
                if(current<0&&!snapshot.path.empty()&&!snapshot.ended){for(const auto&t:tracks)if(t.path==snapshot.path){pendingTracks.push_back(t);current=static_cast<int>(pendingTracks.size()-1);break;}}
                tracks=std::move(pendingTracks);queue.Reset(tracks.size(),current);queue.SetMode(settings.mode);selected=current>=0?current:0;reveal();
            }
            auto state=player.Snapshot();
            if(state.ended&&state.completion!=handledEnd){handledEnd=state.completion;change(1,true);state=player.Snapshot();}
            padUpdate(&pad);u64 down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
            if(down&HidNpadButton_Plus)break;
            u64 navigation=down;
            if(down&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right))lastRepeat=begin+armNsToTicks(250'000'000);
            else if(begin>lastRepeat){navigation|=held&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right);lastRepeat=begin+armNsToTicks(100'000'000);}
            if(navigation&HidNpadButton_Up){--selected;reveal();}
            if(navigation&HidNpadButton_Down){++selected;reveal();}
            if(navigation&HidNpadButton_Left)volume(-5);
            if(navigation&HidNpadButton_Right)volume(5);
            if(down&HidNpadButton_A)toggle(selected);
            if(down&HidNpadButton_L)change(-1,false);
            if(down&HidNpadButton_R)change(1,false);
            if(down&HidNpadButton_ZL)player.Seek(std::max(0.0,state.seconds-10));
            if(down&HidNpadButton_ZR)player.Seek(std::min(state.duration,state.seconds+10));
            if(down&HidNpadButton_Y)mode();
            if(down&HidNpadButton_X)startScan();
            View view;view.tracks=&tracks;view.current=queue.Current();view.selected=selected;view.offset=offset;view.volume=settings.volume;view.mode=settings.mode;
            view.paused=state.paused;view.loading=state.loading;view.scanning=scanning;view.seconds=state.seconds;view.duration=state.duration;view.error=state.error;view.ended=state.ended;view.sourceRate=state.rate;
            if(!state.paused&&!state.ended&&!state.loading&&state.error.empty())animation=std::fmod(animation+dt*0.9,6.283185307);
            view.animation=animation;
            HidTouchScreenState touch{};bool haveTouch=hidGetTouchScreenStates(&touch,1)>0&&touch.count>0;
            if(haveTouch){
                lastTouchX=touch.touches[0].x;lastTouchY=touch.touches[0].y;
                if(!touching){touching=true;dragging=false;touchX=lastTouchX;touchY=lastTouchY;}
                if(std::abs(lastTouchY-touchY)>28||std::abs(lastTouchX-touchX)>28)dragging=true;
            }else if(touching){
                touching=false;
                if(dragging&&touchX>=816){int steps=(touchY-lastTouchY)/64;offset=std::max(0,std::min(offset+steps,std::max(0,static_cast<int>(tracks.size())-VisibleRows)));selected=std::max(offset,std::min(selected,offset+VisibleRows-1));}
                else if(!dragging||(touchY>=508&&touchY<=540&&touchX<780)){
                    auto hit=HitTest(lastTouchX,lastTouchY,view);
                    switch(hit.action){case Action::Song:play(hit.index);break;case Action::Toggle:toggle(hit.index);break;case Action::Previous:change(-1,false);break;case Action::Next:change(1,false);break;
                        case Action::Seek:player.Seek(state.duration*hit.fraction);break;case Action::Mode:mode();break;case Action::VolumeDown:volume(-5);break;case Action::VolumeUp:volume(5);break;default:break;}
                }
            }
            u32 stride=0;auto pixels=static_cast<uint32_t*>(framebufferBegin(&fb,&stride));renderer.Draw(pixels,stride,view);framebufferEnd(&fb);
            u64 elapsed=armTicksToNs(svcGetSystemTick()-begin);if(elapsed<33'333'333)svcSleepThread(static_cast<s64>(33'333'333-elapsed));
        }
        if(scanner.joinable())scanner.join();
        SaveSettings(Config,settings);appletSetAutoSleepDisabled(false);
    }
    framebufferClose(&fb);if(plReady)plExit();return 0;
}
