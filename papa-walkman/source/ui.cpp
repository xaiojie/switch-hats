#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
namespace papa {
namespace {
uint32_t Color(unsigned rgb){return 0xff000000u|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);}
const uint32_t bg=Color(0xf5f2eb),ink=Color(0x283938),muted=Color(0x7c837c),orange=Color(0xe76f3f),cream=Color(0xe8e2d5),white=Color(0xfffdf7);
struct Canvas {
    uint32_t* pixels;int stride;
    void Dot(int x,int y,uint32_t c,int alpha=255){
        if(x<0||y<0||x>=ScreenWidth||y>=ScreenHeight||alpha<=0)return;
        uint32_t& dst=pixels[y*stride+x];
        if(alpha>=255){dst=c;return;}
        uint32_t r=(((c&255)*alpha+(dst&255)*(255-alpha))/255);
        uint32_t g=((((c>>8)&255)*alpha+((dst>>8)&255)*(255-alpha))/255);
        uint32_t b=((((c>>16)&255)*alpha+((dst>>16)&255)*(255-alpha))/255);
        dst=0xff000000u|r|(g<<8)|(b<<16);
    }
    void Box(int x,int y,int w,int h,uint32_t c){
        int right=std::min(ScreenWidth,x+w),bottom=std::min(ScreenHeight,y+h);
        for(int yy=std::max(0,y);yy<bottom;++yy) for(int xx=std::max(0,x);xx<right;++xx) pixels[yy*stride+xx]=c;
    }
    void Round(int x,int y,int w,int h,int radius,uint32_t c){
        radius=std::max(0,std::min(radius,std::min(w,h)/2));
        if(!radius){Box(x,y,w,h,c);return;}
        for(int yy=0;yy<h;++yy){
            float dy=yy<radius?radius-yy-0.5f:(yy>=h-radius?yy-(h-radius)+0.5f:0);
            float inset=dy>0?radius-std::sqrt(std::max(0.f,radius*radius-dy*dy)):0;
            int edge=static_cast<int>(std::ceil(inset));
            Box(x+edge,y+yy,w-edge*2,1,c);
            if(edge>0){int a=static_cast<int>((edge-inset)*255);Dot(x+edge-1,y+yy,c,a);Dot(x+w-edge,y+yy,c,a);}
        }
    }
    void Circle(float x,float y,float r,uint32_t c){
        for(int yy=static_cast<int>(y-r-1);yy<=y+r+1;++yy) for(int xx=static_cast<int>(x-r-1);xx<=x+r+1;++xx){
            float dx=xx+0.5f-x,dy=yy+0.5f-y;float a=r+0.5f-std::sqrt(dx*dx+dy*dy);
            if(a>0)Dot(xx,yy,c,static_cast<int>(std::min(a,1.f)*255));
        }
    }
    void Line(float x,float y,float ex,float ey,float width,uint32_t color){
        float dx=ex-x,dy=ey-y,length=dx*dx+dy*dy;
        for(int yy=static_cast<int>(std::min(y,ey)-width);yy<=std::max(y,ey)+width;++yy)
          for(int xx=static_cast<int>(std::min(x,ex)-width);xx<=std::max(x,ex)+width;++xx){
            float t=length>0?std::max(0.f,std::min(1.f,((xx+0.5f-x)*dx+(yy+0.5f-y)*dy)/length)):0;
            float ax=xx+0.5f-x-t*dx,ay=yy+0.5f-y-t*dy;
            float coverage=width/2+0.5f-std::sqrt(ax*ax+ay*ay);
            if(coverage>0)Dot(xx,yy,color,static_cast<int>(std::min(coverage,1.f)*255));
          }
    }
    void Play(int x,int y,int size,uint32_t c,bool reverse=false){
        for(int xx=0;xx<size;++xx){int half=static_cast<int>((size-xx)*0.66f);Box(reverse?x-xx:x+xx,y-half,1,half*2+1,c);}
    }
};
uint32_t NextCodepoint(const char*& s){
    auto first=static_cast<unsigned char>(*s++); if(first<128)return first;
    int length=first>=0xf0?3:first>=0xe0?2:first>=0xc2?1:0;
    if(!length)return '?';
    uint32_t value=first&((1u<<(6-length))-1);
    for(int i=0;i<length;++i){unsigned char c=static_cast<unsigned char>(*s);if((c&0xc0)!=0x80)return '?';value=(value<<6)|(c&63);++s;}
    return value;
}
struct Glyph{int width=0,height=0,x=0,y=0,advance=0;std::vector<unsigned char> alpha;};
}
struct Renderer::Impl{
    std::vector<stbtt_fontinfo> fonts;
    std::map<std::pair<int,uint32_t>,Glyph> glyphs;
    Glyph& Get(uint32_t code,int size){
        auto key=std::make_pair(size,code); auto found=glyphs.find(key);if(found!=glyphs.end())return found->second;
        Glyph g;
        if(!fonts.empty()){
            const stbtt_fontinfo* font=&fonts[0];
            for(const auto&f:fonts)if(stbtt_FindGlyphIndex(&f,static_cast<int>(code))){font=&f;break;}
            float scale=stbtt_ScaleForPixelHeight(font,static_cast<float>(size));int advance=0,bearing=0;
            stbtt_GetCodepointHMetrics(font,static_cast<int>(code),&advance,&bearing);
            g.advance=static_cast<int>(advance*scale+0.5f);
            unsigned char* data=stbtt_GetCodepointBitmap(font,0,scale,static_cast<int>(code),&g.width,&g.height,&g.x,&g.y);
            if(data){g.alpha.assign(data,data+g.width*g.height);stbtt_FreeBitmap(data,nullptr);}
        }else g.advance=size/2;
        if(glyphs.size()>2048)glyphs.clear();
        return glyphs.emplace(key,std::move(g)).first->second;
    }
    int Measure(const std::string&text,int size){int width=0;const char*s=text.c_str();while(*s)width+=Get(NextCodepoint(s),size).advance;return width;}
    void Text(Canvas&c,int x,int baseline,const std::string&text,int size,uint32_t color,int maxWidth=1200){
        int end=x+maxWidth;const char*s=text.c_str();
        while(*s){auto cp=NextCodepoint(s);auto&g=Get(cp,size);
            if(x+g.advance>end){auto&dots=Get(0x2026,size);for(int yy=0;yy<dots.height;++yy)for(int xx=0;xx<dots.width;++xx)c.Dot(std::min(x,end-dots.advance)+dots.x+xx,baseline+dots.y+yy,color,dots.alpha[yy*dots.width+xx]);break;}
            for(int yy=0;yy<g.height;++yy)for(int xx=0;xx<g.width;++xx)c.Dot(x+g.x+xx,baseline+g.y+yy,color,g.alpha[yy*g.width+xx]);
            x+=g.advance;
        }
    }
    void Reel(Canvas&c,float x,float y,float angle){
        c.Circle(x,y,63,Color(0x182827));c.Circle(x,y,59,Color(0x8d8170));
        c.Circle(x,y,52,Color(0xd6c7a8));c.Circle(x,y,48,Color(0xe7d9bd));
        c.Circle(x,y,32,Color(0x293b38));
        for(int i=0;i<6;++i){float a=angle+i*3.14159265f/3;
            c.Line(x+std::cos(a)*20,y+std::sin(a)*20,x+std::cos(a)*35,y+std::sin(a)*35,9,Color(0xd9c7a5));}
        c.Circle(x,y,16,Color(0x172927));c.Circle(x,y,7,Color(0x68726a));
        c.Circle(x-2,y-3,3,Color(0x8c958a));
    }
};
Renderer::Renderer():p_(new Impl){} Renderer::~Renderer()=default;
bool Renderer::AddFont(const void* data,size_t size){
    if(!data||size<12)return false;
    auto bytes=static_cast<const unsigned char*>(data);int offset=stbtt_GetFontOffsetForIndex(bytes,0);
    if(offset<0||static_cast<size_t>(offset)>=size)return false;
    stbtt_fontinfo font{};if(!stbtt_InitFont(&font,bytes,offset))return false;
    p_->fonts.push_back(font);p_->glyphs.clear();return true;
}
void Renderer::Draw(uint32_t* pixels,int strideBytes,const View& v){
    if(!pixels||strideBytes<ScreenWidth*4)return;
    Canvas c{pixels,strideBytes/4};c.Box(0,0,ScreenWidth,ScreenHeight,bg);
    auto text=[&](int x,int y,const std::string&s,int size,uint32_t color,int w=1200){p_->Text(c,x,y,s,size,color,w);};
    c.Round(40,34,44,44,13,orange);c.Circle(53,56,7,white);c.Circle(71,56,7,white);c.Line(53,56,71,56,2,orange);
    text(98,65,"爬爬的随身听",34,ink);text(100,88,"PAPA WALKMAN  /  YOUR MUSIC, YOUR MOMENT",12,muted);
    const char*state=v.scanning?"正在扫描曲库":!v.error.empty()?"需要留意":v.loading?"正在打开音乐":v.current<0?"等待播放":v.ended?"播放结束":v.paused?"已暂停":"正在播放";
    c.Round(1082,39,158,38,19,Color(0xe9e8df));c.Circle(1103,58,4,v.current>=0&&!v.paused&&!v.ended?Color(0x668568):orange);text(1117,64,state,17,ink,112);
    text(40,120,"NOW PLAYING",14,muted);text(688,120,"VOL. 01",13,muted);
    c.Round(40,143,740,268,28,Color(0xe2ddd2));c.Round(40,135,740,268,28,Color(0xd4cfc1));c.Round(42,136,736,263,27,cream);
    c.Round(70,162,680,211,19,Color(0x1c302e));c.Round(73,165,674,205,17,Color(0x2d4140));
    c.Round(92,181,636,45,7,Color(0xf4eddd));c.Box(92,213,636,13,orange);
    text(109,204,"PAPA  /  ORIGINAL MIXTAPE",16,ink);text(641,204,"SIDE A",15,ink);
    c.Round(339,264,143,45,10,Color(0x172d2a));c.Box(357,280,107,11,Color(0x665e4a));
    p_->Reel(c,246,292,static_cast<float>(v.animation));p_->Reel(c,574,292,static_cast<float>(v.animation));
    c.Line(246,350,574,350,2,Color(0x8a8068));
    c.Circle(111,345,5,Color(0x182c29));c.Circle(709,345,5,Color(0x182c29));
    for(int x:{57,763})for(int y:{151,385}){c.Circle(x,y,3,Color(0xa5a493));c.Line(x-2,y,x+2,y,1,Color(0x6b7468));}
    text(85,389,"STEREO",11,muted);text(641,389,"COMPACT AUDIO",11,muted);
    bool have=v.tracks&&v.current>=0&&static_cast<size_t>(v.current)<v.tracks->size();
    std::string title=have?(*v.tracks)[v.current].title:"选择一首，开始听歌";
    std::string artist=have?(*v.tracks)[v.current].artist:"把 MP3 放进 SD 卡的 music 文件夹";
    text(50,455,title,38,ink,710);text(52,488,artist,22,muted,660);
    double fraction=v.duration>0?std::max(0.0,std::min(1.0,v.seconds/v.duration)):0;
    c.Round(52,520,704,6,3,Color(0xdcded3));if(fraction>0)c.Round(52,520,std::max(6,static_cast<int>(704*fraction)),6,3,orange);
    c.Circle(static_cast<float>(52+704*fraction),523,6,orange);
    text(52,554,TimeLabel(v.seconds),17,muted);auto duration=TimeLabel(v.duration);text(756-p_->Measure(duration,17),554,duration,17,muted);
    c.Circle(410,600,37,Color(0xdfcdbc));c.Circle(410,596,37,orange);
    if(have&&!v.paused&&!v.ended){c.Round(399,584,7,23,2,white);c.Round(414,584,7,23,2,white);}else c.Play(403,596,20,white);
    c.Play(310,596,14,ink,true);c.Box(293,586,3,20,ink);
    c.Play(499,596,14,ink);c.Box(516,586,3,20,ink);
    c.Round(50,577,153,40,20,Color(0xe9e6dc));text(69,603,ModeName(v.mode),19,ink,126);
    c.Circle(631,596,16,Color(0xe7e5da));c.Line(625,596,637,596,2,ink);
    char volume[20];std::snprintf(volume,sizeof(volume),"%d%%",v.volume);text(662,603,volume,21,ink,70);
    c.Circle(753,596,16,Color(0xe7e5da));c.Line(747,596,759,596,2,ink);c.Line(753,590,753,602,2,ink);
    c.Round(816,143,424,494,24,Color(0xe8e4da));c.Round(816,135,424,494,24,white);
    text(842,177,"我的曲库",27,ink);size_t count=v.tracks?v.tracks->size():0;
    char total[24];std::snprintf(total,sizeof(total),"%u 首",static_cast<unsigned>(count));text(1140,175,total,17,muted,82);
    c.Box(840,193,376,1,Color(0xeae8e0));
    if(count==0){
        c.Circle(1028,309,34,Color(0xf4e9dc));
        c.Line(1031,292,1031,319,3,orange);c.Line(1031,292,1042,296,4,orange);c.Circle(1026,319,6,orange);
        text(908,385,v.scanning?"正在寻找音乐…":"还没有音乐",25,ink,294);
        text(859,423,"把 MP3 放入 /music 文件夹",20,muted,350);
        text(915,459,"然后按 X 重新扫描",19,muted,286);
    }else for(int row=0;row<VisibleRows;++row){
        int index=v.offset+row;if(index<0||static_cast<size_t>(index)>=count)break;
        int y=202+row*64;bool playing=index==v.current,selected=index==v.selected;
        if(selected)c.Round(832,y,392,60,12,Color(0xe9b89b));
        if(playing||selected)c.Round(833,y+1,390,58,11,playing?Color(0xf9e8dc):Color(0xf5f2eb));
        if(playing){for(int b=0;b<3;++b){int h=8+static_cast<int>((std::sin(v.animation*3+b*2)+1)*8);if(v.paused||v.ended)h=7+b*4;c.Round(850+b*5,y+30-h/2,3,h,1,orange);}}
        else{char number[12];std::snprintf(number,sizeof(number),"%02d",index+1);text(848,y+35,number,16,muted,30);}
        const auto&t=(*v.tracks)[index];text(888,y+28,t.title,24,playing?orange:ink,316);text(888,y+49,t.artist,17,muted,316);
    }
    if(count>VisibleRows){int h=std::max(16,static_cast<int>(384.0*VisibleRows/count));int yy=202+static_cast<int>((384-h)*static_cast<double>(std::max(0,v.offset))/std::max<size_t>(1,count-VisibleRows));c.Round(1231,yy,3,h,1,Color(0xd3d5c9));}
    if(!v.error.empty()){c.Round(40,635,1200,24,8,Color(0xf8e4d8));text(54,653,v.error,18,Color(0xa8532f),1168);}
    c.Box(40,669,1200,1,Color(0xdedfd4));
    text(40,697,"↑↓ 选曲    A 播放/暂停    L/R 切歌    ZL/ZR 快进退    ←→ 音量    Y 模式    X 扫描",19,muted,1090);
    text(1151,697,"+ 退出",19,ink,89);
}
Hit HitTest(int x,int y,const View&v){
    if(x<0||y<0||x>=ScreenWidth||y>=ScreenHeight)return {};
    if(x>=832&&x<1224&&y>=202&&y<586&&v.tracks){int index=v.offset+(y-202)/64;if(index>=0&&static_cast<size_t>(index)<v.tracks->size())return {Action::Song,index,0};}
    if(x>=52&&x<=756&&y>=508&&y<=540&&v.duration>0)return {Action::Seek,-1,(x-52)/704.0};
    if(y>=560&&y<=634){
        if(x>=370&&x<=450)return {Action::Toggle,v.current>=0?v.current:v.selected,0};
        if(x>=280&&x<=333)return {Action::Previous,-1,0};
        if(x>=487&&x<=537)return {Action::Next,-1,0};
        if(x>=50&&x<=203)return {Action::Mode,-1,0};
        if(x>=610&&x<=649)return {Action::VolumeDown,-1,0};
        if(x>=733&&x<=776)return {Action::VolumeUp,-1,0};
    }
    return {};
}
}
