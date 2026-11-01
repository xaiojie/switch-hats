// Native theme compositions. Content stays live: album art, title, lyrics and playback.
const auto l=GetPlayerLayout(theme);
auto bold=[&](int x,int y,const std::string&s,int size,uint32_t color,int width){
    const int weight=size>=80?3:size>=42?2:1;
    for(int offset=0;offset<weight;++offset){text(x+offset,y,s,size,color,width-offset);text(x+offset,y+1,s,size,color,width-offset);}
};
auto wrapped=[&](int x,int y,const std::string&s,int size,int lineHeight,int width,int rows,uint32_t color){
    auto lines=p_->Lines(s,size,width,rows);
    for(size_t i=0;i<lines.size();++i)bold(x,y+int(i)*lineHeight,lines[i],size,color,width);
};
auto iconSurface=[&](const Rect&r,Action a){if(held(a))c.Round(r.x,r.y,r.width,r.height,16,pal.pressed);};
auto heart=[&](const Rect&r){
    iconSurface(r,Action::Favorite);int cx=r.x+r.width/2,cy=r.y+r.height/2;
    auto color=v.favorite?orange:muted;float w=v.favorite?5:2.5f;
    float lastX=float(cx),lastY=cy-5.f;
    for(int step=1;step<=48;++step){float t=step*6.2831853f/48.f;
        float x=cx+15*std::pow(std::sin(t),3),y=cy-(12*std::cos(t)-5*std::cos(2*t)-2*std::cos(3*t)-std::cos(4*t));
        c.Line(lastX,lastY,x,y,w,color);lastX=x;lastY=y;
    }
};
auto queueIcon=[&](const Rect&r){
    iconSurface(r,Action::PageQueue);int x=r.x+r.width/2-13,y=r.y+r.height/2-10;
    c.Line(x,y,x+24,y,3,muted);c.Line(x,y+9,x+16,y+9,3,muted);c.Line(x,y+18,x+9,y+18,3,muted);c.Play(x+21,y+16,9,muted);
};
if(theme==Theme::NightRecord){
    // Transparent pre-rendered disc is decoded once in Renderer::Impl, behind the live sleeve.
    p_->Image(c,516,128,362,362,p_->vinyl.get());
}else if(theme==Theme::AlbumLiner){
    c.Round(l.cover.x+14,l.cover.y+8,l.cover.width,l.cover.height,5,pal.soft);
    c.Round(l.cover.x+6,l.cover.y-5,l.cover.width,l.cover.height,5,pal.surface);
}
cover(l.cover.x,l.cover.y,l.cover.width,l.cover.height,v.coverImage);
if(v.tape){
    c.Round(l.cover.x,l.cover.y,l.cover.width,l.cover.height,16,cream);
    tape(l.cover.x+l.cover.width/12,l.cover.y+l.cover.height/4,l.cover.width*5/6,l.cover.height/2);
    text(l.cover.x+24,l.cover.y+48,"PAPA / SIDE A",20,muted,l.cover.width-48);
}
if(held(Action::Tape)){
    c.Round(l.cover.x+16,l.cover.y+l.cover.height-58,l.cover.width-32,42,12,pal.soft);
    text(l.cover.x+30,l.cover.y+l.cover.height-30,v.tape?"切换到专辑封面":"切换到磁带",19,ink,l.cover.width-60);
}
static const std::string emptyTitle="选择一首，开始听歌";
const std::string& title=have?playing->title:emptyTitle;
std::string artist=have&&!playing->artist.empty()?playing->artist:"你的音乐，随身带上";
std::string album=have&&!playing->album.empty()?playing->album:"PAPA WALKMAN";
if(theme==Theme::AlbumLiner){
    int size=p_->Measure(title,66,480)<=480?66:46;
    wrapped(612,204,title,size,size+6,480,2,ink);
    text(612,p_->Lines(title,size,480,2).size()>1?286:252,artist+"  /  "+album,25,muted,628);
}else if(theme==Theme::NightRecord){
    text(48,209,"NOW PLAYING",16,orange,300);
    wrapped(48,279,title,56,62,294,2,ink);
    int artistY=p_->Lines(title,56,294,2).size()>1?365:338;
    text(48,artistY,artist,25,muted,296);text(48,artistY+32,album,22,muted,296);
}else{
    bold(40,557,title,48,ink,374);text(42,598,artist+"  /  "+album,24,muted,374);
}
heart(l.favorite);queueIcon(l.queue);
std::string before,line,after;
const bool timedLyrics=HasLyrics(v)&&!v.lyricData->lines.empty();
if(HasLyrics(v)){
    if(!timedLyrics){
        if(p_->plainCache!=v.lyricData->plain){p_->plainCache=v.lyricData->plain;p_->plainLines=WrapPlainLyrics(p_->plainCache);}
        if(!p_->plainLines.empty())line=p_->plainLines[0];
        if(p_->plainLines.size()>1)after=p_->plainLines[1];
        before="歌词 · 无时间标签";
    }
}else{
    before=v.artLoading?"正在寻找歌词…":have?"未找到这首歌的歌词":"从曲库选择音乐";
    line=have?"音乐继续，\n放轻松听。":"把音乐带上";
    after=have?"有些歌曲只有音乐，也很好。":"SD 卡 / 云端音乐库";
}
iconSurface(l.lyrics,Action::PageLyrics);
if(timedLyrics){
    if(theme==Theme::AlbumLiner){
        drawTimedLyrics({612,302,628,184},397,42,29,76,2);
    }else if(theme==Theme::NightRecord){
        c.Circle(960,294,5,orange);
        drawTimedLyrics({978,198,262,230},306,38,24,82,2);
    }else{
        c.Circle(530,278,11,orange);
        drawTimedLyrics({554,132,686,356},307,91,29,142,2);
    }
}else if(theme==Theme::AlbumLiner){
    text(612,335,before,29,muted,628);
    wrapped(612,397,line,42,48,628,2,ink);
    text(612,477,after,28,muted,628);
}else{
    // A phrase break makes punctuated current lyrics read naturally at display scale.
    if(line.find('\n')==std::string::npos)for(const std::string punctuation:{"，",",","；"}){auto pos=line.find(punctuation);if(pos!=std::string::npos){line.replace(pos,punctuation.size(),"\n");break;}}
    if(theme==Theme::NightRecord){
        text(978,225,before,24,muted,262);c.Circle(960,278,5,orange);
        wrapped(978,290,line,38,53,262,2,ink);text(978,398,after,24,muted,262);
    }else{
        text(560,165,before,29,muted,680);c.Circle(570,200,11,orange);
        wrapped(554,307,line,91,106,686,2,ink);text(560,479,after,29,muted,680);
    }
}
if(v.artLoading&&!v.coverImage){
    c.Round(l.cover.x+12,l.cover.y+l.cover.height-48,l.cover.width-24,36,10,pal.surface);
    text(l.cover.x+24,l.cover.y+l.cover.height-24,"正在寻找专辑封面…",18,muted,l.cover.width-48);
}
// All three progress tracks share the exact normalized touch interval, including endpoints.
double fraction=v.seeking?v.seekFraction:v.duration>0?v.seconds/v.duration:0;
fraction=std::isfinite(fraction)?std::clamp(fraction,0.0,1.0):0;
const int seekY=l.seek.y+17,progress=int((l.seek.width-1)*fraction);
if(held(Action::Seek)||v.seeking)c.Round(l.seek.x-8,l.seek.y,l.seek.width+16,l.seek.height,12,pal.selected);
c.Round(l.seek.x,seekY,l.seek.width,6,3,pal.track);
if(progress>0)c.Round(l.seek.x,seekY,progress,6,3,orange);
if(theme==Theme::LyricTheatre)c.Round(l.seek.x+progress-7,seekY-4,15,15,2,orange);
else c.Circle(float(l.seek.x+progress),float(seekY+3),v.seeking?10:8,orange);
std::string elapsed=TimeLabel(v.seeking?v.duration*fraction:v.seconds),duration=v.durationKnown?TimeLabel(v.duration):"--:--";
if(theme==Theme::NightRecord){text(58,seekY+10,elapsed,19,muted,56);text(1180,seekY+10,duration,19,muted,64);}
else {text(l.seek.x,l.seek.y+51,elapsed,20,muted,100);text(l.seek.x+l.seek.width-p_->Measure(duration,20),l.seek.y+51,duration,20,muted,100);}
auto transport=[&](const Rect&r,Action action){
    bool toggle=action==Action::Toggle;uint32_t fill=theme==Theme::LyricTheatre?ink:orange;
    if(toggle)c.Round(r.x,r.y,r.width,r.height,theme==Theme::AlbumLiner?18:r.height/2,held(action)?pal.pressed:fill);
    else iconSurface(r,action);
    uint32_t color=toggle?(held(action)?ink:pal.onAccent):ink;int cx=r.x+r.width/2,cy=r.y+r.height/2;
    if(toggle){if(have&&!v.paused&&!v.ended){c.Round(cx-13,cy-17,9,34,3,color);c.Round(cx+4,cy-17,9,34,3,color);}else c.Play(cx-8,cy,26,color);}
    else if(action==Action::Previous){c.Play(cx+9,cy,21,color,true);c.Round(cx-20,cy-17,4,34,2,color);}
    else {c.Play(cx-12,cy,21,color);c.Round(cx+15,cy-17,4,34,2,color);}
};
transport(l.previous,Action::Previous);transport(l.toggle,Action::Toggle);transport(l.next,Action::Next);
