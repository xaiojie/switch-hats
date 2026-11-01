#include "lyric_motion.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL lyric motion line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool near(double a,double b){return std::abs(a-b)<0.00001;}
int main(){
    Track track{"sdmc:/music/night.mp3","晚风","爬爬"};track.id="sd:night";
    Lyrics lyrics=ParseLyrics("[00:00]路灯留住晚风\n[00:01]脚步慢一点\n[00:02]音乐先到\n[00:03]把今晚装进口袋\n[00:04]明天再出发");
    View v;v.playing=&track;v.lyricData=&lyrics;v.paused=false;v.duration=10;v.seconds=.9;
    // Direct string/index replacement fails this: the start remains at the old
    // baseline, intermediate frames move, and the exact endpoint stops redraws.
    LyricMotion motion;auto f=motion.Update(v,10);CHECK(near(f.position,0)&&!f.active);
    v.seconds=1;f=motion.Update(v,10.1);CHECK(near(f.position,0)&&f.active);
    v.seconds=1.125;f=motion.Update(v,10.225);CHECK(f.position>.3&&f.position<.95&&f.active);
    auto quarter=f.position;v.seconds=1.25;f=motion.Update(v,10.35);CHECK(f.position>quarter&&f.position<1&&f.active);
    v.seconds=1.5;f=motion.Update(v,10.6);CHECK(near(f.position,1)&&!f.active);
    f=motion.Update(v,60);CHECK(near(f.position,1)&&!f.active);

    // Retargeting to a rapid adjacent cue must start at the already displayed
    // fractional position rather than snapping back to the former target.
    Lyrics rapid=ParseLyrics("[00:00]晚风\n[00:01]脚步\n[00:01.15]音乐\n[00:01.3]夜色");
    v.lyricData=&rapid;v.seconds=.9;LyricMotion fast;fast.Update(v,0);
    v.seconds=1;fast.Update(v,.1);v.seconds=1.14;f=fast.Update(v,.24);double continuous=f.position;
    v.seconds=1.15;f=fast.Update(v,.24);CHECK(near(f.position,continuous)&&f.active);
    v.seconds=1.25;f=fast.Update(v,.34);CHECK(f.position>continuous&&f.position<2&&f.active);
    v.seconds=1.3;double second=fast.Update(v,.35).position;
    f=fast.Update(v,.85);CHECK(second<3&&near(f.position,3)&&!f.active);

    // Bilingual LRC keeps original/translation at the same timestamp. Natural
    // playback can advance across multiple rows without being a playback seek.
    Lyrics bilingual=ParseLyrics("[00:01]Evening breeze\n[00:01]晚风\n[00:02]Slow steps\n[00:02]慢慢走");
    v.lyricData=&bilingual;v.seconds=1.9;LyricMotion grouped;
    CHECK(near(grouped.Update(v,0).position,1));
    v.seconds=2;f=grouped.Update(v,.1);CHECK(near(f.position,1)&&f.active);
    v.seconds=2.2;f=grouped.Update(v,.3);CHECK(f.position>1&&f.position<3&&f.active);
    v.seconds=2.5;f=grouped.Update(v,.6);CHECK(near(f.position,3)&&!f.active);

    // Identity/document/theme/page changes discard obsolete transitions.
    v.lyricData=&lyrics;v.seconds=.9;
    for(int change=0;change<5;++change){
        LyricMotion reset;v.theme=Theme::AlbumLiner;v.page=Page::Player;v.seconds=.9;reset.Update(v,0);
        v.seconds=1;CHECK(reset.Update(v,.1).active);
        Lyrics replacement=lyrics;Track other=track;other.id="sd:other";
        if(change==0)v.playing=&other;
        if(change==1)v.lyricData=&replacement;
        if(change==2)v.theme=Theme::NightRecord;
        if(change==3)v.page=Page::Lyrics;
        if(change==4){other.id=track.id;other.path="sdmc:/music/replaced.mp3";v.playing=&other;}
        f=reset.Update(v,.2);CHECK(near(f.position,1)&&!f.active);
        v.playing=&track;v.lyricData=&lyrics;
    }
    // A metadata copy of the same track is not a track change.
    v.theme=Theme::AlbumLiner;v.page=Page::Player;v.seconds=.9;LyricMotion same;same.Update(v,0);
    v.seconds=1;same.Update(v,.1);Track metadata=track;metadata.title="新的标题";v.playing=&metadata;
    f=same.Update(v,.2);CHECK(f.position>0&&f.position<1&&f.active);v.playing=&track;

    // Playback seeks, preview release and timing correction are instantaneous,
    // including an adjacent-line seek whose time jump is larger than wall time.
    v.seconds=.9;LyricMotion seek;seek.Update(v,0);v.seconds=1;seek.Update(v,.1);
    v.seconds=4;f=seek.Update(v,.2);CHECK(near(f.position,4)&&!f.active);
    v.seconds=.4;f=seek.Update(v,.3);CHECK(near(f.position,0)&&!f.active);
    v.seconds=1.9;f=seek.Update(v,.4);CHECK(near(f.position,1)&&!f.active);
    v.seeking=true;v.seekFraction=.32;f=seek.Update(v,.5);CHECK(near(f.position,3)&&!f.active);
    v.seekFraction=.42;f=seek.Update(v,.6);CHECK(near(f.position,4)&&!f.active);
    v.seeking=false;v.seconds=4.2;f=seek.Update(v,.7);CHECK(near(f.position,4)&&!f.active);
    v.lyricOffset=-2;f=seek.Update(v,.8);CHECK(near(f.position,2)&&!f.active);v.lyricOffset=0;

    // Manual expanded scroll is an immediate first-row + 2 anchor, including
    // the final row at the top. Returning to following snaps back to playback.
    v.page=Page::Lyrics;v.seconds=1.2;v.lyricScroll=3;LyricMotion manual;
    f=manual.Update(v,0);CHECK(near(f.position,5)&&!f.active);
    v.lyricScroll=4;f=manual.Update(v,.1);CHECK(near(f.position,6)&&!f.active);
    v.lyricScroll=-1;f=manual.Update(v,.2);CHECK(near(f.position,1)&&!f.active);
    v.page=Page::Library;v.seconds=2;f=manual.Update(v,.3);CHECK(near(f.position,2)&&!f.active);
    v.seconds=3;f=manual.Update(v,1.3);CHECK(near(f.position,3)&&!f.active);

    // Pause/buffering/loading never extrapolate song time from the wall clock.
    // A movement already in flight may settle, avoiding a perpetual paint loop.
    for(int state=0;state<4;++state){
        v.page=Page::Player;v.seconds=.9;LyricMotion stopped;stopped.Update(v,0);
        v.seconds=1;stopped.Update(v,.1);
        v.paused=state==0;v.buffering=state==1;v.loading=state==2;v.ended=state==3;
        f=stopped.Update(v,.3);CHECK(f.position>0&&f.position<1&&f.active);
        f=stopped.Update(v,100);CHECK(near(f.position,1)&&!f.active);
        f=stopped.Update(v,200);CHECK(near(f.position,1)&&!f.active);
        v.paused=v.buffering=v.loading=v.ended=false;
    }
    // Missing/plain lyrics and invalid clock values cannot schedule animations.
    Lyrics plain;plain.plain="没有时间标签\n也是音乐";LyricMotion fallback;
    v.lyricData=&plain;f=fallback.Update(v,0);CHECK(near(f.position,0)&&!f.active);
    v.lyricData=nullptr;f=fallback.Update(v,1);CHECK(near(f.position,0)&&!f.active);
    v.lyricData=&lyrics;v.seconds=std::numeric_limits<double>::quiet_NaN();
    f=fallback.Update(v,2);CHECK(std::isfinite(f.position)&&!f.active);
    v.seconds=1;f=fallback.Update(v,std::numeric_limits<double>::infinity());CHECK(std::isfinite(f.position)&&!f.active);
    std::puts("PASS lyric motion: finite eased scrolling, continuous retarget, resets, seeks, manual rows and no paused drift");
}
