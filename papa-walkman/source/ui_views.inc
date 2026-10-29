// Layout and interaction maps share these explicit 1280x720 page coordinates.
void Renderer::Draw(uint32_t*pixels,int strideBytes,const View&v){
    if(!pixels||strideBytes<ScreenWidth*4)return;
    Canvas c{pixels,strideBytes/4};c.Box(0,0,ScreenWidth,ScreenHeight,bg);
    auto text=[&](int x,int y,const std::string&s,int size,uint32_t color,int width=1200){p_->Text(c,x,y,s,size,color,width);};
    auto button=[&](int x,int y,int w,int h,const std::string&label,bool active=false){
        c.Round(x,y,w,h,std::min(14,h/2),active?orange:Color(0xe9e6dc));
        int tw=p_->Measure(label,18);text(x+std::max(9,(w-tw)/2),y+h/2+6,label,18,active?white:ink,w-18);
    };
    auto tape=[&](int x,int y,int w,int h){
        c.Round(x,y,w,h,12,Color(0x2d4140));c.Round(x+12,y+12,w-24,std::max(4,h/6),4,cream);
        c.Circle(x+w*.30f,y+h*.61f,std::min(w,h)*.19f,Color(0xd6c7a8));c.Circle(x+w*.70f,y+h*.61f,std::min(w,h)*.19f,Color(0xd6c7a8));
        c.Circle(x+w*.30f,y+h*.61f,std::min(w,h)*.08f,ink);c.Circle(x+w*.70f,y+h*.61f,std::min(w,h)*.08f,ink);
        c.Box(x+12,y+12+std::max(4,h/6),w-24,std::max(2,h/20),orange);
    };
    auto cover=[&](int x,int y,int w,int h,const CoverImage*image){c.Round(x,y,w,h,12,Color(0xe7e0d1));if(image)c.Image(x,y,w,h,image,12);else tape(x+10,y+10,w-20,h-20);};
    const Track*playing=v.playing;
    if(!playing&&v.tracks&&v.current>=0&&size_t(v.current)<v.tracks->size())playing=&(*v.tracks)[v.current];
    bool have=playing!=nullptr;
    c.Round(40,34,44,44,13,orange);c.Circle(53,56,7,white);c.Circle(71,56,7,white);c.Line(53,56,71,56,2,orange);
    text(98,65,"爬爬的随身听",34,ink);text(100,88,"PAPA WALKMAN / VOL. 03",12,muted);
    const char*state=v.scanning?"正在整理曲库":!v.error.empty()?"需要留意":v.loading?"正在打开音乐":v.buffering?"正在缓冲":!have?"等待播放":v.ended?"播放结束":v.paused?"已暂停":"正在播放";
    c.Round(1082,39,158,38,19,Color(0xe9e8df));c.Circle(1103,58,4,have&&!v.paused&&!v.ended&&!v.buffering?Color(0x668568):orange);text(1117,64,state,17,ink,112);
    const char*tabs[]={"曲库","正在播放","歌词","播放队列","设置"};
    for(int i=0;i<5;++i)button(40+i*124,98,112,29,tabs[i],int(v.page)==i);
    if(v.page==Page::Player)button(688,98,92,29,v.tape?"显示封面":"显示磁带");
    if(v.resumeAvailable)button(1090,98,150,29,"继续上次");
    size_t count=v.tracks?v.tracks->size():0;
    auto thumb=[&](int row)->const CoverImage*{return size_t(row)<v.thumbnails.size()?v.thumbnails[row].get():nullptr;};
    if(v.page==Page::Player){
        c.Round(40,143,740,268,28,Color(0xe2ddd2));c.Round(40,135,740,268,28,cream);
        if(v.tape){
            c.Round(70,162,680,211,19,Color(0x1c302e));c.Round(73,165,674,205,17,Color(0x2d4140));
            c.Round(92,181,636,45,7,Color(0xf4eddd));c.Box(92,213,636,13,orange);
            text(109,204,"PAPA / ORIGINAL MIXTAPE",16,ink);text(641,204,"SIDE A",15,ink);
            c.Round(339,264,143,45,10,Color(0x172d2a));c.Box(357,280,107,11,Color(0x665e4a));
            p_->Reel(c,246,292,float(v.animation));p_->Reel(c,574,292,float(v.animation));c.Line(246,350,574,350,2,Color(0x8a8068));
            for(int x:{57,763})for(int y:{151,385}){c.Circle(x,y,3,Color(0xa5a493));c.Line(x-2,y,x+2,y,1,Color(0x6b7468));}
            text(85,389,"STEREO",11,muted);text(620,389,"COMPACT AUDIO",11,muted);
        }else{
            cover(64,157,222,222,v.coverImage);text(316,181,have&&playing->source==SourceKind::Cloud?"CLOUD LIBRARY / 云端":"SD LIBRARY / SD 卡",13,muted,440);
            text(316,235,have?playing->album:"属于你的音乐时光",31,ink,430);
            text(318,275,have?playing->artist:"选择一首，开始听歌",22,muted,420);
            text(318,321,"ALBUM / 专辑",13,muted,410);
            if(v.sourceRate>0)text(318,350,"MP3 · "+std::to_string(v.sourceRate/1000)+" kHz · Stereo",18,ink,410);
            if(v.buffering)text(318,378,"缓冲中，音乐稍后继续…",18,orange,420);
        }
        text(50,455,have?playing->title:"选择一首，开始听歌",38,ink,710);
        text(52,488,have?playing->artist+" · "+playing->album:"把 MP3 放入 /music，或添加云端音乐库",21,muted,704);
        double progress=v.duration>0&&std::isfinite(v.duration)?std::max(0.0,std::min(1.0,v.seconds/v.duration)):0;
        c.Round(52,520,704,6,3,Color(0xdcded3));if(progress>0)c.Round(52,520,std::max(6,int(704*progress)),6,3,orange);c.Circle(float(52+704*progress),523,6,orange);
        text(52,554,TimeLabel(v.seconds),17,muted);auto duration=v.durationKnown?TimeLabel(v.duration):"--:--";text(756-p_->Measure(duration,17),554,duration,17,muted);
        c.Round(816,143,424,486,24,Color(0xe8e4da));c.Round(816,135,424,486,24,white);
        if(v.lyricData&&(!v.lyricData->lines.empty()||!v.lyricData->plain.empty())){
            text(842,177,"此刻的歌词",27,ink);button(1110,150,106,30,"展开");c.Box(840,193,376,1,Color(0xeae8e0));
            if(!v.lyricData->lines.empty()){
                int current=v.lyricData->Current(v.seconds,v.lyricOffset);int first=std::max(0,current-2);
                for(int row=0;row<5&&first+row<int(v.lyricData->lines.size());++row){int index=first+row;text(850,246+row*61,v.lyricData->lines[index].text,index==current?27:23,index==current?orange:muted,350);}
            }else{std::string plain=v.lyricData->plain;size_t pos=0;for(int row=0;row<5&&pos<plain.size();++row){size_t end=plain.find('\n',pos);text(850,246+row*61,plain.substr(pos,end==std::string::npos?end:end-pos),23,muted,350);if(end==std::string::npos)break;pos=end+1;}}
        }else{
            text(842,177,count?"接下来":"还没有音乐",27,ink);text(1135,175,std::to_string(count)+" 首",17,muted,84);c.Box(840,193,376,1,Color(0xeae8e0));
            if(!count){tape(958,270,140,104);text(866,423,"选择歌曲或添加云端音乐库",20,muted,344);text(902,459,"封面和歌词可由网页补充",18,muted,310);}
            for(int row=0;row<VisibleRows;++row){int index=v.offset+row;if(index<0||size_t(index)>=count)break;int y=202+row*64;bool active=index==v.current,selected=index==v.selected;
                if(selected)c.Round(832,y,392,60,12,Color(0xe9b89b));
                if(active||selected)c.Round(833,y+1,390,58,11,active?Color(0xf9e8dc):bg);
                text(848,y+35,std::to_string(index+1),16,muted,30);const auto&t=(*v.tracks)[index];text(888,y+28,t.title,24,active?orange:ink,316);text(888,y+49,t.artist,17,muted,316);
            }
        }
    }else if(v.page==Page::Library){
        const char*labels[]={"SD 卡","云端","收藏","歌单"};for(int i=0;i<4;++i)button(40+i*120,145,110,36,labels[i],int(v.librarySource)==i);
        const char*groups[]={"歌曲","专辑","歌手"};for(int i=0;i<3;++i)button(560+i*100,145,90,36,groups[i],int(v.grouping)==i&&v.librarySource!=LibrarySource::Playlists);
        button(890,145,350,36,v.query.empty()?"搜索歌曲、歌手、专辑…":v.query);
        if(v.showingGroups&&v.groups){
            for(int row=0;row<6;++row){int index=v.offset+row;if(index<0||size_t(index)>=v.groups->size())break;int x=40+(row%3)*408,y=202+(row/3)*177;const auto&g=(*v.groups)[index];
                c.Round(x,y,384,158,20,index==v.selected?Color(0xf6d8c2):white);cover(x+17,y+20,96,96,thumb(row));
                text(x+128,y+48,g.title,27,ink,236);text(x+128,y+80,g.subtitle,18,muted,236);text(x+128,y+121,std::to_string(g.count)+" 首歌曲",18,orange,232);
            }
            if(v.groups->empty()){text(470,340,"这里还没有内容",30,ink,650);text(430,387,v.librarySource==LibrarySource::Playlists?"先把喜欢的歌曲放进队列，再保存歌单":"试试换个来源，或重新扫描曲库",21,muted,730);}
        }else{
            for(int row=0;row<6;++row){int index=v.offset+row;if(index<0||size_t(index)>=count)break;int y=201+row*58;const auto&t=(*v.tracks)[index];bool active=have&&t.id==playing->id;
                c.Round(40,y,1200,54,12,index==v.selected?Color(0xf6e4d5):white);cover(52,y+6,42,42,thumb(row));
                text(110,y+24,t.title,24,active?orange:ink,450);text(110,y+45,t.artist,16,muted,450);text(588,y+34,t.album,19,muted,354);
                text(977,y+33,t.source==SourceKind::SD?"SD 卡":"云端",17,muted,72);text(1090,y+33,t.duration>0?TimeLabel(t.duration):"--:--",17,muted,90);
            }
            if(!count){tape(550,238,180,130);text(470,423,v.scanning?"正在整理音乐…":v.librarySource==LibrarySource::Cloud?"添加你的云端音乐库":"这里还没有歌曲",30,ink,700);text(390,468,v.librarySource==LibrarySource::Cloud?"在设置里添加 HTTP/HTTPS 曲库或 M3U 地址":"检查 music 文件夹，或尝试清除搜索条件",21,muted,820);}
        }
    }else if(v.page==Page::Lyrics){
        c.Round(40,144,1200,414,22,white);text(64,185,"歌词",28,ink);button(840,145,104,36,"提前 0.5s");button(950,145,140,36,"重置偏移");button(1096,145,144,36,"延后 0.5s");
        cover(64,211,174,174,v.coverImage);text(64,425,have?playing->title:"尚未播放",24,ink,190);text(64,458,have?playing->artist:"",18,muted,190);
        char shift[80];std::snprintf(shift,sizeof(shift),"偏移 %+.1f 秒",v.lyricOffset);text(64,501,shift,17,muted,190);
        if(v.lyricData&&!v.lyricData->lines.empty()){
            int current=v.lyricData->Current(v.seconds,v.lyricOffset);int anchor=v.lyricScroll>=0?v.lyricScroll:std::max(0,current-2);
            for(int row=0;row<6&&anchor+row<int(v.lyricData->lines.size());++row){int i=anchor+row;if(i<0)continue;text(296,232+row*56,v.lyricData->lines[i].text,i==current?36:27,i==current?orange:muted,910);}
        }else if(v.lyricData&&!v.lyricData->plain.empty()){
            if(p_->plainCache!=v.lyricData->plain){p_->plainCache=v.lyricData->plain;p_->plainLines=WrapPlainLyrics(p_->plainCache);}
            int first=ClampLyricScroll(v.lyricData,std::max(0,v.lyricScroll));for(int row=0;row<6&&first+row<int(p_->plainLines.size());++row)text(296,232+row*56,p_->plainLines[first+row],27,ink,910);
        }else{text(510,325,"暂无歌词",34,ink,560);text(350,383,"可读取同名 LRC、歌曲内嵌歌词或云端歌词",23,muted,850);text(385,430,"也可以通过电脑网页为这首歌补充歌词",21,muted,810);}
    }else if(v.page==Page::Queue){
        text(40,169,"播放队列",28,ink);text(220,167,std::to_string(count)+" 首 · 可混合 SD 与云端歌曲",18,muted,820);
        for(int row=0;row<6;++row){int i=v.offset+row;if(i<0||size_t(i)>=count)break;int y=202+row*58;const auto&t=(*v.tracks)[i];
            c.Round(40,y,1200,54,12,i==v.selected?Color(0xf6e4d5):white);text(58,y+34,std::to_string(i+1),17,muted,40);text(108,y+25,t.title,24,i==v.current?orange:ink,650);text(108,y+45,t.artist+(t.source==SourceKind::Cloud?" · 云端":" · SD 卡"),16,muted,650);
            button(1037,y+9,47,34,"↑");button(1092,y+9,47,34,"↓");button(1147,y+9,77,34,"移除");
        }
        if(!count){text(450,340,"把下一首喜欢的歌放进来",31,ink,730);text(440,390,"在曲库中选中歌曲，按 X 加入队列",22,muted,760);}
    }else{
        text(40,172,"云端音乐源",28,ink);text(238,171,"保存一次，下次直接听",19,muted,720);button(1060,145,180,38,"添加音乐源");
        if(v.sources)for(int row=0;row<4;++row){int i=v.offset+row;if(i<0||size_t(i)>=v.sources->size())break;const auto&s=(*v.sources)[i];int y=206+row*58;
            c.Round(40,y,1200,53,12,i==v.selected?Color(0xf6e4d5):white);text(60,y+25,s.name,23,ink,800);text(60,y+45,s.enabled?"HTTP / HTTPS / M3U · 已启用":"已停用",15,muted,790);button(1040,y+9,80,34,"编辑");button(1130,y+9,94,34,"移除");
        }
        if(!v.sources||v.sources->empty()){text(65,264,"还没有云端音乐源",25,ink);text(65,305,"添加曲库地址，或在电脑网页配置后通过 Switch Link 同步",21,muted,1100);}
        c.Round(40,460,1200,94,18,white);text(64,491,"缓存空间",23,ink);text(64,527,"已使用 "+std::to_string(v.cacheBytes/(1024*1024))+" MB · 当前播放数据会保留",17,muted,680);
        text(640,500,"上限",18,muted);button(780,478,44,44,"-");text(844,507,std::to_string(v.cacheLimit/(1024*1024))+" MB",22,ink,120);button(952,478,44,44,"+");button(1040,484,174,42,"清理闲置缓存");
    }
    c.Circle(410,600,37,Color(0xdfcdbc));c.Circle(410,596,37,orange);
    if(have&&!v.paused&&!v.ended){c.Round(399,584,7,23,2,white);c.Round(414,584,7,23,2,white);}else c.Play(403,596,20,white);
    c.Play(310,596,14,ink,true);c.Box(293,586,3,20,ink);c.Play(499,596,14,ink);c.Box(516,586,3,20,ink);
    button(50,577,153,40,ModeName(v.mode));c.Circle(631,596,16,Color(0xe7e5da));c.Line(625,596,637,596,2,ink);text(662,603,std::to_string(v.volume)+"%",21,ink,70);c.Circle(753,596,16,Color(0xe7e5da));c.Line(747,596,759,596,2,ink);c.Line(753,590,753,602,2,ink);
    if(v.page==Page::Player)button(1100,585,124,34,v.favorite?"已收藏":"收藏歌曲",v.favorite);
    if(v.page==Page::Queue){button(816,579,136,36,"保存为歌单");button(960,579,136,36,"导出歌单");button(1104,579,136,36,"导入歌单");}
    if(v.page==Page::Library){if(!v.query.empty()||v.showingGroups||!v.subtitle.empty())button(816,579,136,36,"返回 / 清除");button(960,579,136,36,"加入队列");button(1104,579,136,36,v.librarySource==LibrarySource::Playlists?"删除歌单":"收藏 / 取消");}
    if(v.page==Page::Settings){button(816,579,136,36,"扫描 SD 卡");button(960,579,136,36,"刷新云端");button(1104,579,136,36,"导入歌单");}
    if(v.page!=Page::Player&&have){text(52,652,(playing->source==SourceKind::Cloud?"云端 · ":"SD · ")+playing->title+" / "+playing->artist,16,muted,704);double f=v.duration>0&&std::isfinite(v.duration)?std::max(0.0,std::min(1.0,v.seconds/v.duration)):0;c.Round(816,645,424,4,2,Color(0xdcded3));if(f>0)c.Round(816,645,std::max(4,int(424*f)),4,2,orange);}
    std::string notice=!v.error.empty()?v.error:v.notice;
    if(!notice.empty()){c.Round(40,635,1200,24,8,Color(0xf8e4d8));text(54,653,notice,18,Color(0xa8532f),1168);}
    c.Box(40,669,1200,1,Color(0xdedfd4));
    text(40,697,v.page==Page::Library?(v.librarySource==LibrarySource::Playlists?"↑↓ 选歌单   A 打开/播放   Y 删除歌单   - 切换页面":"↑↓ 选曲   A 播放   X 入队   Y 收藏   - 切换页面   L/R 切歌"):"A 播放/暂停   L/R 切歌   ZL/ZR 快进退   ←→ 音量   - 切换页面",18,muted,1090);text(1151,697,"+ 退出",19,ink,89);
}

Hit HitTest(int x,int y,const View&v){
    if(x<0||y<0||x>=ScreenWidth||y>=ScreenHeight)return {};
    if(y>=98&&y<=127){for(int i=0;i<5;++i)if(x>=40+i*124&&x<152+i*124)return {static_cast<Action>(int(Action::PageLibrary)+i),-1,0};if(v.page==Page::Player&&x>=688&&x<780)return {Action::Tape};if(v.resumeAvailable&&x>=1090&&x<1240)return {Action::Resume};}
    if(v.page==Page::Library){
        if(y>=145&&y<=181){for(int i=0;i<4;++i)if(x>=40+i*120&&x<150+i*120)return {static_cast<Action>(int(Action::SourceSD)+i)};for(int i=0;i<3;++i)if(x>=560+i*100&&x<650+i*100)return {static_cast<Action>(int(Action::GroupSongs)+i)};if(x>=890&&x<1240)return {Action::Search};}
        if(v.showingGroups&&v.groups&&x>=40&&x<1240&&y>=202&&y<537){int col=(x-40)/408,row=(y-202)/177;int i=v.offset+row*3+col;if(col<3&&row<2&&size_t(i)<v.groups->size())return {Action::GroupItem,i};}
        else if(!v.showingGroups&&v.tracks&&x>=40&&x<1240&&y>=201&&y<549){int i=v.offset+(y-201)/58;if(i>=0&&size_t(i)<v.tracks->size())return {Action::Song,i};}
    }else if(v.page==Page::Queue&&v.tracks&&x>=40&&x<1240&&y>=202&&y<550){int i=v.offset+(y-202)/58;if(i>=0&&size_t(i)<v.tracks->size()){if(x>=1147)return {Action::QueueRemove,i};if(x>=1092&&x<1139)return {Action::QueueDown,i};if(x>=1037&&x<1084)return {Action::QueueUp,i};return {Action::QueueSong,i};}}
    else if(v.page==Page::Settings){
        if(x>=1060&&x<1240&&y>=145&&y<183)return {Action::SourceAdd};
        if(v.sources&&y>=206&&y<438){int i=v.offset+(y-206)/58;if(i>=0&&size_t(i)<v.sources->size()){if(x>=1130&&x<1224)return {Action::SourceDelete,i};if(x>=1040&&x<1120)return {Action::SourceEdit,i};}}
        if(y>=478&&y<526){if(x>=780&&x<824)return {Action::CacheDown};if(x>=952&&x<996)return {Action::CacheUp};if(x>=1040&&x<1214)return {Action::ClearCache};}
    }else if(v.page==Page::Lyrics&&y>=145&&y<181){if(x>=840&&x<944)return {Action::LyricEarlier};if(x>=950&&x<1090)return {Action::LyricReset};if(x>=1096&&x<1240)return {Action::LyricLater};}
    else if(v.page==Page::Player){
        if(x>=816&&x<1240&&y>=135&&y<580){if(v.lyricData&&(!v.lyricData->lines.empty()||!v.lyricData->plain.empty()))return {Action::PageLyrics};if(v.tracks&&y>=202&&y<586){int i=v.offset+(y-202)/64;if(i>=0&&size_t(i)<v.tracks->size())return {Action::Song,i};}}
        if(x>=52&&x<=756&&y>=508&&y<=540&&v.duration>0&&v.seekable)return {Action::Seek,-1,(x-52)/704.0};
        if(x>=1100&&x<1224&&y>=585&&y<619)return {Action::Favorite};
    }
    if(y>=579&&y<=619&&x>=816&&x<1240){int col=(x-816)/144;if(col>=3)return {};
        if(v.page==Page::Queue)return {col==0?Action::SavePlaylist:col==1?Action::ExportPlaylist:Action::ImportPlaylist};
        if(v.page==Page::Library)return {col==0?Action::Back:col==1?Action::Enqueue:v.librarySource==LibrarySource::Playlists?Action::DeletePlaylist:Action::Favorite};
        if(v.page==Page::Settings)return {col==0?Action::Refresh:col==1?Action::Refresh:Action::ImportPlaylist,col};
    }
    if(y>=560&&y<=634){if(x>=370&&x<=450)return {Action::Toggle,v.current>=0?v.current:v.selected};if(x>=280&&x<=333)return {Action::Previous};if(x>=487&&x<=537)return {Action::Next};if(x>=50&&x<=203)return {Action::Mode};if(x>=610&&x<=649)return {Action::VolumeDown};if(x>=733&&x<=776)return {Action::VolumeUp};}
    if(v.page!=Page::Player&&x>=816&&x<=1240&&y>=636&&y<=658&&v.duration>0&&v.seekable)return {Action::Seek,-1,(x-816)/424.0};
    return {};
}
