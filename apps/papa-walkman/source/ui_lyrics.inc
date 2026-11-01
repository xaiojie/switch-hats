// Shared by every Player composition and the expanded lyric page. Glyphs are
// cached once at activeSize and sampled at a continuous scale during motion;
// intermediate font sizes never enter the raster cache.
auto drawTimedLyrics=[&](Rect viewport,int anchorY,int activeSize,int neighborSize,int rowPitch,int maxLines){
    if(!v.lyricData||v.lyricData->lines.empty()||viewport.width<=0||viewport.height<=0||rowPitch<=0)return;
    const auto&rows=v.lyricData->lines;
    int current=std::max(0,v.lyricData->Current(v.seconds,v.lyricOffset));
    bool manual=v.page==Page::Lyrics&&v.lyricScroll>=0;
    double position=std::isfinite(v.lyricPosition)&&v.lyricPosition>=0?v.lyricPosition:
        manual?std::min(v.lyricScroll,int(rows.size()-1))+2:current;
    position=std::clamp(position,0.0,double(rows.size()+1));
    // The expanded list starts at row zero until the third cue. This keeps its
    // first manual drag aligned with firstRow=max(0,current-2).
    const double focusPosition=position;
    if(v.page==Page::Lyrics&&!manual)position=std::max(2.0,position);
    const int left=std::max(0,viewport.x),right=std::min(ScreenWidth,viewport.x+viewport.width);
    const int top=std::max(c.clipTop,viewport.y),bottom=std::min(c.clipBottom,viewport.y+viewport.height);
    if(left>=right||top>=bottom)return;
    const int drag=manual?v.scrollPixels:0;
    const int radius=viewport.height/rowPitch+3;
    const int first=std::max(0,int(std::floor(position))-radius);
    const int last=std::min(int(rows.size())-1,int(std::ceil(position))+radius);
    auto tint=[&](double focus){
        uint32_t result=0xff000000u;
        for(int shift:{0,8,16}){
            double low=(muted>>shift)&255,high=(ink>>shift)&255;
            result|=uint32_t(std::lround(low+(high-low)*focus))<<shift;
        }
        return result;
    };
    for(int index=first;index<=last;++index){
        const double distance=index-position;
        double focus=manual?(index==current?1.0:0.0):std::clamp(1-std::abs(index-focusPosition),0.0,1.0);
        focus=focus*focus*(3-2*focus);
        double scale=(neighborSize+(activeSize-neighborSize)*focus)/activeSize;
        double baseline=anchorY+distance*rowPitch+drag;
        double lineHeight=(activeSize+std::max(5,activeSize/7))*scale;
        if(baseline+lineHeight*maxLines<top||baseline-activeSize*scale>=bottom)continue;
        std::string phrase=rows[index].text;
        // The two narrow/editorial Player compositions keep their existing
        // phrase breaks. Layout is stable while a cue grows into the focus row.
        if(v.page==Page::Player&&v.theme!=Theme::AlbumLiner&&phrase.find('\n')==std::string::npos){
            for(const char* punctuation:{"，",",","；"}){
                auto at=phrase.find(punctuation);
                if(at!=std::string::npos){phrase.replace(at,std::strlen(punctuation),"\n");break;}
            }
        }
        auto lines=p_->Lines(phrase,activeSize,viewport.width,maxLines);
        uint32_t color=tint(focus);
        for(size_t line=0;line<lines.size();++line){
            double pen=viewport.x;
            double by=baseline+line*lineHeight;
            const char* cursor=lines[line].c_str();
            while(*cursor&&pen<right){
                auto cp=NextCodepoint(cursor);if(cp<32)cp=' ';
                auto&g=p_->Get(cp,activeSize);
                double gx=pen+g.x*scale,gy=by+g.y*scale;
                int x0=std::max(left,int(std::floor(gx))),x1=std::min(right,int(std::ceil(gx+g.width*scale)));
                int y0=std::max(top,int(std::floor(gy))),y1=std::min(bottom,int(std::ceil(gy+g.height*scale)));
                // Bilinear alpha filtering keeps cached glyph outlines smooth
                // while both their baseline and emphasis move sub-pixel.
                for(int y=y0;y<y1;++y){
                    double sy=(y+.5-gy)/scale-.5;int iy=int(std::floor(sy));double fy=sy-iy;
                    double edge=std::clamp(std::min(y+.5-top,bottom-y-.5)/16.0,0.0,1.0);
                    double opacity=edge*(.76+.24*focus);
                    for(int x=x0;x<x1;++x){
                        double sx=(x+.5-gx)/scale-.5;int ix=int(std::floor(sx));double fx=sx-ix;
                        auto sample=[&](int px,int py)->double{
                            return px>=0&&px<g.width&&py>=0&&py<g.height?g.alpha[size_t(py)*g.width+px]:0;
                        };
                        double alpha=(sample(ix,iy)*(1-fx)+sample(ix+1,iy)*fx)*(1-fy)+
                            (sample(ix,iy+1)*(1-fx)+sample(ix+1,iy+1)*fx)*fy;
                        c.Dot(x,y,color,int(std::lround(alpha*opacity)));
                    }
                }
                pen+=g.advance*scale;
            }
        }
    }
};
