#pragma once
#include "core.hpp"
#include <memory>
namespace papa {
constexpr int ScreenWidth=1280, ScreenHeight=720, VisibleRows=6;
struct View {
    const std::vector<Track>* tracks=nullptr;
    int current=-1, selected=0, offset=0, volume=70;
    Mode mode=Mode::LoopAll;
    bool paused=true, scanning=false, loading=false, ended=false;
    double seconds=0, duration=0, animation=0;
    int sourceRate=0;
    std::string error;
};
enum class Action {None, Song, Toggle, Previous, Next, Seek, Mode, VolumeDown, VolumeUp};
struct Hit {Action action=Action::None; int index=-1; double fraction=0;};
Hit HitTest(int x,int y,const View& view);
class Renderer {
public:
    Renderer(); ~Renderer();
    bool AddFont(const void* bytes, size_t size);
    void Draw(uint32_t* pixels,int strideBytes,const View& view);
private:
    struct Impl; std::unique_ptr<Impl> p_;
};
}
