#pragma once
#include "ui.hpp"

namespace papa {
struct LyricMotionFrame {
    double position=0;
    bool active=false;
};

// Time is injected by the caller; the playback clock remains the only source
// of lyric targets. Wall time is used only to finish a finite visual movement.
class LyricMotion {
public:
    LyricMotionFrame Update(const View& view,double monotonicSeconds);
private:
    LyricMotionFrame Sample(double now) const;
    std::string trackId_,trackPath_;
    const Lyrics* document_=nullptr;
    const LyricLine* lineData_=nullptr;
    size_t lineCount_=0;
    Theme theme_=Theme::AlbumLiner;
    Page page_=Page::Player;
    double from_=0,target_=0,started_=0,lastWall_=0,lastSeconds_=0;
    double userOffset_=0,documentOffset_=0;
    int manual_=-1;
    bool initialized_=false,active_=false,seeking_=false;
};
}
