#include "lyric_motion.hpp"
#include <algorithm>
#include <cmath>

namespace papa {
namespace {
constexpr double Duration=.5;
// Solve x(t) for CSS cubic-bezier(.22,1,.36,1); y(t)=1-(1-t)^3.
// A bounded bisection avoids frame-rate-dependent springs and overshoot.
double Ease(double progress){
    double low=0,high=1;
    for(int i=0;i<20;++i){
        double t=(low+high)*.5,u=1-t;
        double x=3*u*u*t*.22+3*u*t*t*.36+t*t*t;
        if(x<progress)low=t;else high=t;
    }
    double u=1-(low+high)*.5;
    return 1-u*u*u;
}
}
LyricMotionFrame LyricMotion::Sample(double now)const{
    if(!active_||now-started_>=Duration-1e-9)return {target_,false};
    if(now<=started_)return {from_,true};
    return {from_+(target_-from_)*Ease((now-started_)/Duration),true};
}
LyricMotionFrame LyricMotion::Update(const View&v,double now){
    bool validClock=std::isfinite(now)&&std::isfinite(v.seconds);
    if(!std::isfinite(now))now=initialized_?lastWall_:0;
    double seconds=std::isfinite(v.seconds)?std::max(0.0,v.seconds):0;
    if(v.seeking&&std::isfinite(v.seekFraction)&&std::isfinite(v.duration)&&v.duration>0)
        seconds=v.duration*std::clamp(v.seekFraction,0.0,1.0);
    const Track* track=v.playing;
    if(!track&&v.tracks&&v.current>=0&&size_t(v.current)<v.tracks->size())track=&(*v.tracks)[v.current];
    const std::string empty;
    const auto&id=track?track->id:empty;
    const auto&path=track?track->path:empty;
    bool timed=track&&v.lyricData&&!v.lyricData->lines.empty();
    const LyricLine* lineData=timed?v.lyricData->lines.data():nullptr;
    size_t count=timed?v.lyricData->lines.size():0;
    double shift=std::isfinite(v.lyricOffset)?v.lyricOffset:0;
    double docShift=timed&&std::isfinite(v.lyricData->offset)?v.lyricData->offset:0;
    int manual=v.page==Page::Lyrics?v.lyricScroll:-1;
    double target=timed?std::max(0,v.lyricData->Current(seconds,shift)):0;
    if(timed&&manual>=0)target=double(std::min(manual,int(count-1)))+2;
    bool visible=v.page==Page::Player||v.page==Page::Lyrics;
    double wallDelta=now-lastWall_,playDelta=seconds-lastSeconds_;
    // The engine reports completed seeks through its playback clock. Detect
    // discontinuities while allowing modest audio snapshot jitter at boundaries.
    bool jumped=initialized_&&(playDelta<-.08||playDelta>std::max(0.0,wallDelta)+.75);
    bool reset=!initialized_||!validClock||wallDelta<0||!timed||!visible||
        trackId_!=id||trackPath_!=path||document_!=v.lyricData||
        lineData_!=lineData||lineCount_!=count||theme_!=v.theme||page_!=v.page||
        userOffset_!=shift||documentOffset_!=docShift||manual_!=manual||
        v.seeking||seeking_||jumped;
    auto frame=Sample(now);
    if(reset){
        from_=target_=target;active_=false;
    }else if(target!=target_){
        bool running=!v.paused&&!v.buffering&&!v.loading&&!v.ended;
        if(running&&manual<0&&target>target_){
            // Natural playback may cross multiple rows sharing a timestamp
            // (original text plus translation). Seeks were reset above.
            // Rapid cues begin at the currently visible fractional row.
            from_=frame.position;target_=target;started_=now;active_=true;
        }else{
            from_=target_=target;active_=false;
        }
    }else active_=frame.active;
    initialized_=true;trackId_=id;trackPath_=path;document_=v.lyricData;
    lineData_=lineData;lineCount_=count;theme_=v.theme;page_=v.page;
    userOffset_=shift;documentOffset_=docShift;manual_=manual;seeking_=v.seeking;
    lastWall_=now;lastSeconds_=seconds;
    return Sample(now);
}
}
