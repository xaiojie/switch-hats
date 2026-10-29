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
    void Image(int x,int y,int w,int h,const CoverImage*image,int radius=10){
        if(!image||image->width<=0||image->height<=0||image->pixels.empty())return;
        double scale=std::max(w/double(image->width),h/double(image->height));
        double ox=(image->width-w/scale)/2,oy=(image->height-h/scale)/2;
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
            float dx=std::max(float(radius)-xx-.5f,xx+.5f-(w-radius)),dy=std::max(float(radius)-yy-.5f,yy+.5f-(h-radius));
            if(dx>0&&dy>0&&dx*dx+dy*dy>radius*radius)continue;
            int sx=std::max(0,std::min(image->width-1,int(ox+(xx+.5)/scale))),sy=std::max(0,std::min(image->height-1,int(oy+(yy+.5)/scale)));
            uint32_t pixel=image->pixels[size_t(sy)*image->width+sx];Dot(x+xx,y+yy,pixel,int(pixel>>24));
        }
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
    std::string plainCache;
    std::vector<std::string> plainLines;
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
        while(*s){auto cp=NextCodepoint(s);if(cp<32)cp=' ';auto&g=Get(cp,size);
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
std::vector<std::string>WrapPlainLyrics(const std::string&value,size_t columns){
    std::vector<std::string>lines;if(value.empty()||value.size()>1024*1024)return lines;
    columns=std::max<size_t>(2,std::min<size_t>(200,columns));std::string line;size_t width=0;
    const char*s=value.c_str();
    while(*s&&lines.size()<10000){const char*before=s;uint32_t cp=NextCodepoint(s);
        if(cp=='\r')continue;
        if(cp=='\n'){lines.push_back(std::move(line));line.clear();width=0;continue;}
        size_t units=cp>=0x2e80?2:1;if(cp>=0x300&&cp<=0x36f)units=0;
        if(width+units>columns&&!line.empty()){lines.push_back(std::move(line));line.clear();width=0;}
        line.append(before,size_t(s-before));width+=units;
    }
    if(!line.empty()&&lines.size()<10000)lines.push_back(std::move(line));
    return lines;
}
int ClampLyricScroll(const Lyrics*lyrics,int requested){
    if(!lyrics)return 0;
    size_t rows=lyrics->lines.empty()?WrapPlainLyrics(lyrics->plain).size():lyrics->lines.size();
    return std::max(0,std::min(requested,rows?int(rows-1):0));
}
PointerGesture ResolveGesture(int sx,int sy,int ex,int ey,const View&view){
    PointerGesture gesture;
    if(std::abs(ex-sx)<=28&&std::abs(ey-sy)<=28){gesture.hit=HitTest(ex,ey,view);return gesture;}
    if(view.duration>0&&view.seekable){
        if(view.page==Page::Player&&sx>=52&&sx<=756&&sy>=508&&sy<=540){
            gesture.hit={Action::Seek,-1,std::max(0.0,std::min(1.0,(ex-52)/704.0))};return gesture;
        }
        if(view.page!=Page::Player&&view.error.empty()&&view.notice.empty()&&sx>=816&&sx<=1240&&sy>=636&&sy<=658){
            gesture.hit={Action::Seek,-1,std::max(0.0,std::min(1.0,(ex-816)/424.0))};return gesture;
        }
    }
    if(view.page==Page::Lyrics&&sx>=280&&sx<1240&&sy>=200&&sy<558){gesture.scrolling=gesture.lyrics=true;gesture.scrollRows=(sy-ey)/56;return gesture;}
    if((view.page==Page::Library||view.page==Page::Queue)&&sx>=40&&sx<1240&&sy>=201&&sy<550){
        gesture.scrolling=true;gesture.scrollRows=view.showingGroups?(sy-ey)/177*3:(sy-ey)/58;return gesture;
    }
    if(view.page==Page::Settings&&sx>=40&&sx<1240&&sy>=206&&sy<438){gesture.scrolling=true;gesture.scrollRows=(sy-ey)/58;return gesture;}
    if(view.page==Page::Player&&sx>=832&&sx<1224&&sy>=202&&sy<586&&!view.lyricData){gesture.scrolling=true;gesture.scrollRows=(sy-ey)/64;}
    return gesture;
}
#include "ui_views.inc"
}
