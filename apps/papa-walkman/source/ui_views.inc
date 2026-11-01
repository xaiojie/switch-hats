// Drawing and hit testing deliberately share these physical rectangles.
namespace {
bool Within(int x,int y,int left,int top,int width,int height){return Rect{left,top,width,height}.Contains(x,y);}
bool HasLyrics(const View&v){return v.lyricData&&(!v.lyricData->lines.empty()||!v.lyricData->plain.empty());}
constexpr Rect MiniPrevious{892,608,64,64},MiniToggle{972,608,64,64},MiniNext{1052,608,64,64},MiniQueue{1176,608,64,64};
constexpr Rect VolumeMinus{944,224,64,64},VolumePlus{1136,224,64,64},ModeRow{320,308,920,76};
constexpr Rect CacheMinus{944,224,64,64},CachePlus{1136,224,64,64},ClearCacheRow{320,312,920,72};
constexpr Rect RefreshLibrary{320,408,920,80};
constexpr Rect SourceAddButton{920,156,144,64},SourceRefreshButton{1080,156,160,64};
constexpr Rect SourceEditButton{320,496,200,64},SourceDeleteButton{1040,496,200,64};
Rect ThemeChoice(int i){return {320+i*312,424,296,112};}
Rect PlaylistRow(int i){return {320,224+i*104,920,88};}
Rect SettingsTab(int i){return {40,204+i*80,224,64};}
Rect SourceTab(int i){return {40,188+i*72,176,64};}
const Action settingsActions[]={Action::SettingsPlayback,Action::SettingsSources,Action::SettingsStorage,Action::SettingsPlaylists};
const Action sourceActions[]={Action::SourceSD,Action::SourceCloud,Action::SourceFavorites,Action::SourcePlaylists};
const Action playlistActions[]={Action::ImportPlaylist,Action::SavePlaylist,Action::ExportPlaylist};
}
Rect LibraryListRect(){return {256,ListTop,984,ListBottom-ListTop};}
Rect SourceListRect(){return {320,SourceListTop,920,SourceListBottom-SourceListTop};}
int SettingCount(SettingsSection section){
    switch(section){case SettingsSection::Playback:return 6;case SettingsSection::Storage:return 4;case SettingsSection::Playlists:return 3;default:return 0;}
}
Hit SettingAction(const View&v){
    if(v.settingsSection==SettingsSection::Sources){
        if(v.sources&&v.selected>=0&&size_t(v.selected)<v.sources->size())return {Action::SourceEdit,v.selected};
        return {Action::SourceAdd};
    }
    if(v.selected<0||v.selected>=SettingCount(v.settingsSection))return {};
    if(v.settingsSection==SettingsSection::Playback){
        if(v.selected==0)return {Action::VolumeDown};
        if(v.selected==1)return {Action::VolumeUp};
        if(v.selected==2)return {Action::Mode};
        return {Action::CycleTheme,v.selected-3};
    }
    if(v.settingsSection==SettingsSection::Storage){
        if(v.selected==0)return {Action::CacheDown};
        if(v.selected==1)return {Action::CacheUp};
        if(v.selected==2)return {Action::ClearCache};
        return {Action::Refresh};
    }
    return {playlistActions[v.selected]};
}
void Renderer::Draw(uint32_t*pixels,int strideBytes,const View&v){
    if(!pixels||strideBytes<ScreenWidth*4)return;
    const Theme theme=NormalizeTheme(int(v.theme));const auto pal=ThemePalette(theme);
    const auto bg=pal.bg,ink=pal.ink,muted=pal.muted,orange=pal.accent,cream=pal.soft;
    Canvas c{pixels,strideBytes/4};c.Box(0,0,ScreenWidth,ScreenHeight,bg);
    const Hit pressed=HitTest(v.touchX,v.touchY,v);
    auto text=[&](int x,int y,const std::string&s,int size,uint32_t color,int width=1200){p_->Text(c,x,y,s,size,color,width);};
    auto held=[&](Action action,int index=-1){return action!=Action::None&&pressed.action==action&&(index<0||pressed.index==index);};
    auto button=[&](int x,int y,int w,int h,const std::string&label,Action action,bool active=false,int index=-1){
        bool down=held(action,index);
        if(down||active)c.Round(x,y,w,h,14,down?pal.pressed:pal.selected);
        if(label=="-"||label=="+"){int cx=x+w/2,cy=y+h/2;c.Line(cx-8,cy,cx+8,cy,2,ink);
        if(label=="+")c.Line(cx,cy-8,cx,cy+8,2,ink);
        return;}
        if(action==Action::PagePlayer&&label=="返回"){c.Line(x+24,y+h/2-7,x+17,y+h/2,2,muted);c.Line(x+17,y+h/2,x+24,y+h/2+7,2,muted);text(x+38,y+h/2+8,label,22,ink,w-44);return;}
        int size=22,tw=p_->Measure(label,size);text(x+std::max(12,(w-tw)/2),y+h/2+8,label,size,active?orange:ink,w-24);
    };
    auto tape=[&](int x,int y,int w,int h){
        c.Round(x,y,w,h,16,ink);c.Round(x+12,y+12,w-24,h/5,5,cream);c.Box(x+12,y+12+h/5,w-24,std::max(3,h/24),orange);
        c.Circle(x+w*.29f,y+h*.64f,h*.22f,Color(0xd6c7a8));c.Circle(x+w*.71f,y+h*.64f,h*.22f,Color(0xd6c7a8));
        c.Circle(x+w*.29f,y+h*.64f,h*.09f,ink);c.Circle(x+w*.71f,y+h*.64f,h*.09f,ink);
    };
    auto cover=[&](int x,int y,int w,int h,const CoverImage*image){
        c.Round(x,y,w,h,16,pal.soft);
        if(image)p_->Image(c,x,y,w,h,image);else tape(x+w/8,y+h/4,w*3/4,h/2);
    };
    const Track*playing=v.playing;
    if(!playing&&v.tracks&&v.current>=0&&size_t(v.current)<v.tracks->size())playing=&(*v.tracks)[v.current];
    const bool have=playing!=nullptr;const size_t count=v.tracks?v.tracks->size():0;
#include "ui_lyrics.inc"
    auto thumb=[&](int row)->const CoverImage*{return row>=0&&size_t(row)<v.thumbnails.size()?v.thumbnails[row].get():nullptr;};
    auto scrollBar=[&](int total,int visible,int top,int bottom){
        if(total<=visible)return;
        int height=bottom-top,knob=std::max(32,height*visible/total);
        int pos=int((height-knob)*std::clamp(v.offset/double(std::max(1,total-visible)),0.0,1.0));
        c.Round(1235,top,4,height,2,pal.soft);c.Round(1235,top+pos,4,knob,2,muted);
    };
    const char*state=v.scanning?"正在整理曲库":!v.error.empty()?"需要留意":v.loading?"正在打开音乐":v.buffering?"正在缓冲":!have?"等待播放":v.ended?"播放结束":v.paused?"已暂停":"正在播放";
    std::string notice=!v.error.empty()?v.error:v.notice;
    // One centered 64px navigation bar, shared by main and child pages.
    const auto layout=GetPlayerLayout(theme);
    c.Round(40,30,48,48,15,orange);c.Circle(54,54,7,pal.onAccent);c.Circle(74,54,7,pal.onAccent);c.Line(54,54,74,54,2,orange);
    text(104,59,"爬爬的随身听",30,ink,344);text(106,79,"PAPA WALKMAN",12,muted,344);
    c.Round(476,22,332,64,32,pal.soft);
    auto tab=[&](const Rect&r,const char*label,Action action,bool active){
        c.Round(r.x,r.y,r.width,r.height,32,held(action)?pal.pressed:active?(theme==Theme::LyricTheatre?ink:orange):pal.soft);
        text(r.x+(r.width-p_->Measure(label,23))/2,r.y+40,label,23,active&&!held(action)?pal.onAccent:ink,r.width-16);
    };
    const bool playerFamily=v.page==Page::Player||v.page==Page::Lyrics||v.page==Page::Queue;
    tab(layout.libraryTab,"曲库",Action::PageLibrary,v.page==Page::Library);tab(layout.playerTab,"播放",Action::PagePlayer,playerFamily);tab(layout.settingsTab,"设置",Action::PageSettings,v.page==Page::Settings);
    if(!notice.empty()){c.Round(40,86,1200,28,10,pal.errorBg);text(54,107,notice,18,pal.errorInk,1172);}
    else if(v.resumeAvailable)button(900,22,250,64,"继续上次播放",Action::Resume);
    else text(982,105,state,17,muted,258);

    if(v.page==Page::Player){
#include "ui_player.inc"
    }else if(v.page==Page::Library){
        const char*sources[]={"SD 卡","云端音乐","我的收藏","我的歌单"};const char*grouping[]={"歌曲","专辑","歌手"};
        text(48,158,"音乐收藏",28,ink,180);c.Box(231,140,1,412,pal.soft);
        for(int i=0;i<4;++i){auto r=SourceTab(i);bool active=int(v.librarySource)==i;
            if(active||held(sourceActions[i]))c.Round(r.x,r.y,r.width,r.height,12,held(sourceActions[i])?pal.pressed:pal.soft);
            if(active)c.Round(r.x,r.y+20,3,24,1,orange);
            text(r.x+18,r.y+41,sources[i],23,active?ink:muted,r.width-30);
        }
        std::string heading=v.subtitle.empty()?sources[std::clamp(int(v.librarySource),0,3)]:v.subtitle;
        text(256,157,heading,35,ink,620);
        const bool canBack=!v.query.empty()||!v.subtitle.empty();
        if(canBack)button(916,120,132,64,"返回 / 清除",Action::Back);
        if(v.showingGroups&&v.librarySource==LibrarySource::Playlists){if(v.groups&&!v.groups->empty())button(1064,120,176,64,"删除歌单",Action::DeletePlaylist);}
        else if(!v.showingGroups&&count)button(1064,120,176,64,"加入队列",Action::Enqueue);
        if(v.librarySource!=LibrarySource::Playlists){
            for(int i=0;i<3;++i){int x=256+i*108;auto action=i==0?Action::GroupSongs:i==1?Action::GroupAlbums:Action::GroupArtists;
                button(x,176,96,56,grouping[i],action);
                if(int(v.grouping)==i)c.Round(x+16,226,64,3,1,orange);
            }
        }else text(272,213,v.showingGroups?"全部歌单":"歌单歌曲",21,muted,296);
        c.Round(652,180,588,48,16,pal.soft);button(652,176,588,56,v.query.empty()?"搜索歌曲、歌手、专辑":v.query,Action::Search);
        c.clipTop=ListTop;c.clipBottom=ListBottom;
        if(v.showingGroups&&v.groups){
            for(int row=-1;row<3;++row)for(int col=0;col<GroupColumns;++col){int i=v.offset+row*GroupColumns+col;
            if(i<0||size_t(i)>=v.groups->size())continue;
                int x=256+col*500,y=ListTop+row*GroupRowHeight+v.scrollPixels;const auto&g=(*v.groups)[i];
                if(i==v.selected||held(Action::GroupItem,i))c.Round(x,y,476,148,14,held(Action::GroupItem,i)?pal.pressed:pal.soft);
                cover(x+12,y+20,116,116,thumb(row*GroupColumns+col));text(x+148,y+53,g.title,27,ink,304);
                text(x+148,y+88,g.subtitle,20,muted,304);text(x+148,y+124,std::to_string(g.count)+" 首歌曲",18,muted,304);
            }
            if(v.groups->empty()){text(320,358,"这里还没有内容",34,ink,850);text(320,405,v.librarySource==LibrarySource::Playlists?"将喜欢的歌曲加入队列，再到设置中保存歌单。":"试试切换来源，或重新整理曲库。",22,muted,850);}
        }else{
            for(int row=-1;row<=ListVisibleRows;++row){int i=v.offset+row;
            if(i<0||size_t(i)>=count)continue;
            int y=ListTop+row*ListRowHeight+v.scrollPixels;const auto&t=(*v.tracks)[i];
                bool active=have&&t.id==playing->id;
                if(i==v.selected||held(Action::Song,i))c.Round(256,y,972,76,12,held(Action::Song,i)?pal.pressed:pal.soft);
                else c.Box(272,y+75,940,1,pal.soft);
                cover(272,y+10,56,56,thumb(row));text(348,y+33,t.title,27,active?orange:ink,420);text(348,y+59,t.artist,19,muted,420);
                text(804,y+44,t.album,21,muted,286);text(1132,y+44,t.duration>0?TimeLabel(t.duration):"--:--",19,muted,80);
            }
            if(!count){text(320,352,v.scanning?"正在整理你的音乐…":v.librarySource==LibrarySource::Cloud?"把云端音乐带进来":"这里还没有歌曲",34,ink,850);
                text(320,401,v.librarySource==LibrarySource::Cloud?"在设置的「音乐源」中添加你的曲库。":"把音乐放入 music 文件夹，或清除搜索条件。",22,muted,850);}
        }
        c.clipTop=0;c.clipBottom=ScreenHeight;scrollBar(v.showingGroups&&v.groups?int(v.groups->size()):int(count),4,ListTop,ListBottom);
    }else if(v.page==Page::Lyrics){
        button(40,120,136,64,"返回",Action::PagePlayer);text(204,158,"歌词",34,ink,530);text(204,192,have?playing->title:"尚未播放",21,muted,550);
        button(800,136,140,64,"提前 0.5s",Action::LyricEarlier);button(950,136,140,64,"重置偏移",Action::LyricReset);button(1100,136,140,64,"延后 0.5s",Action::LyricLater);
        cover(40,224,240,240,v.coverImage);char shift[80];std::snprintf(shift,sizeof(shift),"歌词偏移 %+.1f 秒",v.lyricOffset);text(42,507,shift,19,muted,255);
        if(HasLyrics(v)){
            if(!v.lyricData->lines.empty())drawTimedLyrics({320,LyricListTop,920,LyricListBottom-LyricListTop},LyricListTop+2*LyricRowHeight+39,38,27,LyricRowHeight,1);
            else{
                c.clipTop=LyricListTop;c.clipBottom=LyricListBottom;
                if(p_->plainCache!=v.lyricData->plain){p_->plainCache=v.lyricData->plain;p_->plainLines=WrapPlainLyrics(p_->plainCache);}
                int first=std::clamp(std::max(0,v.lyricScroll),0,std::max(0,int(p_->plainLines.size())-1));
                for(int row=-1;row<7;++row){int i=first+row;
                if(i>=0&&size_t(i)<p_->plainLines.size())text(340,LyricListTop+row*LyricRowHeight+39+v.scrollPixels,p_->plainLines[i],27,ink,880);}
                c.clipTop=0;c.clipBottom=ScreenHeight;
            }
        }else{text(380,348,v.artLoading?"正在寻找这首歌的歌词…":"未找到这首歌的歌词",34,ink,820);text(380,398,"有些歌曲只有音乐，也很好。",23,muted,820);text(380,438,"歌曲内嵌或同名 LRC 歌词会自动显示在这里。",20,muted,820);}
    }else if(v.page==Page::Queue){
        button(40,120,136,64,"返回",Action::PagePlayer);text(204,158,"播放队列",34,ink,430);text(204,194,std::to_string(count)+" 首 · 接下来听",21,muted,430);
        if(v.selected>=0&&size_t(v.selected)<count){button(680,144,140,64,"播放选中",Action::QueueSong,false,v.selected);button(832,144,120,64,"上移",Action::QueueUp,false,v.selected);button(964,144,120,64,"下移",Action::QueueDown,false,v.selected);button(1096,144,144,64,"移除",Action::QueueRemove,false,v.selected);}
        c.clipTop=ListTop;c.clipBottom=ListBottom;
        for(int row=-1;row<=ListVisibleRows;++row){int i=v.offset+row;
        if(i<0||size_t(i)>=count)continue;
        int y=ListTop+row*ListRowHeight+v.scrollPixels;const auto&t=(*v.tracks)[i];
            if(i==v.selected||held(Action::QueueSelect,i))c.Round(40,y,1188,76,12,held(Action::QueueSelect,i)?pal.pressed:pal.soft);else c.Box(60,y+75,1148,1,pal.soft);
            text(64,y+44,std::to_string(i+1),21,muted,44);cover(124,y+10,56,56,thumb(row));text(204,y+33,t.title,27,i==v.current?orange:ink,780);text(204,y+59,t.artist,19,muted,780);
            text(1076,y+44,i==v.current?"正在播放":t.duration>0?TimeLabel(t.duration):"",19,i==v.current?orange:muted,136);
        }
        if(!count){text(320,352,"把下一首喜欢的歌放进来",34,ink,880);text(320,401,"在曲库中选择歌曲，按 X 或轻触「加入队列」。",22,muted,880);}
        c.clipTop=0;c.clipBottom=ScreenHeight;scrollBar(int(count),ListVisibleRows,ListTop,ListBottom);
    }else if(v.page==Page::Settings){
        const char*sections[]={"播放与外观","音乐源","存储与维护","歌单"};const char*descriptions[]={"让音乐按你的习惯播放","连接你的云端音乐收藏","整理曲库，留出一点空间","把喜欢的歌曲收在一起"};
        int section=std::clamp(int(v.settingsSection),0,3);text(48,158,"设置",34,ink,200);c.Box(287,140,1,412,pal.soft);
        for(int i=0;i<4;++i){auto r=SettingsTab(i);bool active=section==i;
        if(active||held(settingsActions[i]))c.Round(r.x,r.y,r.width,r.height,12,held(settingsActions[i])?pal.pressed:pal.soft);
            if(active)c.Round(r.x,r.y+20,3,24,1,orange);
            text(r.x+18,r.y+41,sections[i],23,active?ink:muted,r.width-30);}
        text(320,158,sections[section],35,ink,568);text(320,192,descriptions[section],21,muted,568);
        Hit selected=SettingAction(v);
        auto settingButton=[&](const Rect&r,const std::string&label,Action action,int index=-1){
            bool focus=selected.action==action&&selected.index==index;button(r.x,r.y,r.width,r.height,label,action,focus,index);
        };
        auto settingRow=[&](const Rect&r,const std::string&title,const std::string&detail,Action action,int index=-1){
            bool focus=selected.action==action&&selected.index==index,down=held(action,index);
            if(focus||down)c.Round(r.x,r.y,r.width,r.height,12,down?pal.pressed:pal.soft);else c.Box(r.x,r.y+r.height-1,r.width,1,pal.soft);
            text(r.x+20,r.y+34,title,25,ink,r.width-84);text(r.x+20,r.y+63,detail,19,muted,r.width-84);int cx=r.x+r.width-32,cy=r.y+r.height/2;c.Line(cx-4,cy-7,cx+3,cy,2,muted);c.Line(cx+3,cy,cx-4,cy+7,2,muted);
        };
        if(v.settingsSection==SettingsSection::Playback){
            text(340,257,"音量",26,ink,300);text(340,283,"方向键 ← / → 随时调整",18,muted,530);
            settingButton(VolumeMinus,"-",Action::VolumeDown);text(1032,266,std::to_string(v.volume)+"%",28,ink,98);settingButton(VolumePlus,"+",Action::VolumeUp);
            settingRow(ModeRow,"播放模式",ModeName(v.mode),Action::Mode);text(340,413,"外观主题",20,muted,400);
            for(int i=0;i<3;++i){auto r=ThemeChoice(i);auto colors=ThemePalette(Theme(i));bool chosen=int(theme)==i,focus=selected.action==Action::CycleTheme&&selected.index==i;
                c.Round(r.x,r.y,r.width,r.height,14,held(Action::CycleTheme,i)?pal.pressed:focus?pal.selected:pal.soft);
                c.Round(r.x+18,r.y+20,44,44,12,colors.bg);c.Circle(r.x+40,r.y+42,10,colors.accent);
                text(r.x+78,r.y+45,ThemeName(Theme(i)),24,ink,r.width-90);text(r.x+78,r.y+77,chosen?"正在使用":"轻触切换",18,chosen?orange:muted,r.width-90);
                if(chosen)c.Round(r.x+18,r.y+r.height-9,r.width-36,3,1,orange);
            }
        }else if(v.settingsSection==SettingsSection::Sources){
            button(SourceAddButton.x,SourceAddButton.y,SourceAddButton.width,SourceAddButton.height,"+ 添加",Action::SourceAdd);button(SourceRefreshButton.x,SourceRefreshButton.y,SourceRefreshButton.width,SourceRefreshButton.height,"刷新曲库",Action::Refresh);
            c.clipTop=SourceListTop;c.clipBottom=SourceListBottom;
            if(v.sources&&!v.sources->empty())for(int row=-1;row<=SourceVisibleRows;++row){int i=v.offset+row;
            if(i<0||size_t(i)>=v.sources->size())continue;
            int y=SourceListTop+row*SourceRowHeight+v.scrollPixels;const auto&s=(*v.sources)[i];
                if(i==v.selected||held(Action::SourceEdit,i))c.Round(320,y,908,76,12,held(Action::SourceEdit,i)?pal.pressed:pal.soft);else c.Box(340,y+75,868,1,pal.soft);
                c.Circle(344,y+37,4,s.enabled?orange:muted);text(366,y+33,s.name,26,ink,796);text(366,y+60,s.enabled?"HTTP / HTTPS / M3U · 已启用":"已停用",18,muted,796);c.Line(1192,y+31,1199,y+38,2,muted);c.Line(1199,y+38,1192,y+45,2,muted);
            }else{text(340,330,"还没有云端音乐源",32,ink,820);text(340,377,"添加曲库或 M3U 地址，音乐就会出现在曲库。",22,muted,820);text(340,420,"也可通过 Switch Link 同步配置。",20,muted,820);}
            c.clipTop=0;c.clipBottom=ScreenHeight;scrollBar(v.sources?int(v.sources->size()):0,SourceVisibleRows,SourceListTop,SourceListBottom);
            if(v.sources&&v.selected>=0&&size_t(v.selected)<v.sources->size()){
                button(SourceEditButton.x,SourceEditButton.y,SourceEditButton.width,SourceEditButton.height,"编辑选中源",Action::SourceEdit,false,v.selected);button(SourceDeleteButton.x,SourceDeleteButton.y,SourceDeleteButton.width,SourceDeleteButton.height,"移除选中源",Action::SourceDelete,false,v.selected);
            }
        }else if(v.settingsSection==SettingsSection::Storage){
            text(340,257,"缓存上限",26,ink,300);text(340,283,"已缓存 "+std::to_string(v.cacheBytes/(1024*1024))+" MB",18,muted,530);
            settingButton(CacheMinus,"-",Action::CacheDown);text(1020,266,std::to_string(v.cacheLimit/(1024*1024))+" MB",24,ink,116);settingButton(CachePlus,"+",Action::CacheUp);
            settingRow(ClearCacheRow,"清理闲置缓存","正在播放的数据会保留",Action::ClearCache);
            settingRow(RefreshLibrary,"刷新曲库","重新扫描 SD 卡并更新云端音乐源",Action::Refresh);
        }else{
            settingRow(PlaylistRow(0),"导入歌单","从 SD 卡读取已保存的歌单",Action::ImportPlaylist);
            settingRow(PlaylistRow(1),"队列存为歌单","把此刻的播放顺序保存下来",Action::SavePlaylist);
            settingRow(PlaylistRow(2),"导出当前队列","将歌曲列表保存到 SD 卡",Action::ExportPlaylist);
        }
    }
    if(v.page!=Page::Player){
        // A quiet strip keeps music available without competing with the page.
        c.Box(40,600,1200,1,pal.track);cover(40,612,56,56,v.coverImage);
        text(116,638,have?playing->title:"你的下一首好歌",24,ink,660);text(116,662,have?playing->artist:"从曲库开始播放",18,muted,660);
        auto transport=[&](const Rect&r,Action a){
            bool toggle=a==Action::Toggle;
            if(held(a))c.Round(r.x,r.y,r.width,r.height,16,pal.pressed);
            int cx=r.x+r.width/2,cy=r.y+r.height/2;uint32_t color=ink;
            if(toggle){c.Circle(cx,cy,25,theme==Theme::LyricTheatre?ink:orange);color=pal.onAccent;
                if(have&&!v.paused&&!v.ended){c.Round(cx-9,cy-11,6,22,2,color);c.Round(cx+3,cy-11,6,22,2,color);}else c.Play(cx-5,cy,17,color);
            }else if(a==Action::Previous){c.Play(cx+7,cy,16,color,true);c.Round(cx-15,cy-12,3,24,1,color);}
            else{c.Play(cx-9,cy,16,color);c.Round(cx+12,cy-12,3,24,1,color);}
        };
        transport(MiniPrevious,Action::Previous);transport(MiniToggle,Action::Toggle);transport(MiniNext,Action::Next);
        if(held(Action::PageQueue))c.Round(MiniQueue.x,MiniQueue.y,MiniQueue.width,MiniQueue.height,16,pal.pressed);
        c.Line(1194,630,1218,630,2,muted);c.Line(1194,639,1210,639,2,muted);c.Line(1194,648,1203,648,2,muted);c.Play(1215,647,8,muted);
    }
    std::string hints;
    if(v.page==Page::Library)hints=v.showingGroups?(v.librarySource==LibrarySource::Playlists?"方向键 选择   A 打开   Y 删除歌单   ZL/ZR 来源":"方向键 选择   A 打开   ZL/ZR 来源   右摇杆 分组"):"↑↓ 选曲   A 播放   X 入队   Y 收藏   ZL/ZR 来源";
    else if(v.page==Page::Queue)hints="↑↓ 选择   A 播放   X 移除   ZL/ZR 排序   B 返回";
    else if(v.page==Page::Lyrics)hints="↑↓ 滚动   A 播放/暂停   ←→ 偏移   X 重置   B 返回";
    else if(v.page==Page::Settings)hints=v.settingsSection==SettingsSection::Sources?"↑↓ 选来源   A 编辑   X 添加   Y 刷新   ZL/ZR 分类   ←→ 音量":"↑↓ 选择   A 确定   ZL/ZR 分类   ←→ 音量   右摇杆 主题";
    else hints="A 播放/暂停   X 队列   Y 歌词   L/R 切歌   ZL/ZR 快进退";
    text(40,705,hints+"   - 切换页面",17,muted,1096);text(1152,705,"+ 退出",19,ink,88);
}

Hit HitTest(int x,int y,const View&v){
    if(x<0||y<0||x>=ScreenWidth||y>=ScreenHeight)return {};
    const auto l=GetPlayerLayout(v.theme);
    if(l.libraryTab.Contains(x,y))return {Action::PageLibrary};
    if(l.playerTab.Contains(x,y))return {Action::PagePlayer};
    if(l.settingsTab.Contains(x,y))return {Action::PageSettings};
    if(v.error.empty()&&v.notice.empty()&&v.resumeAvailable&&Within(x,y,900,22,250,64))return {Action::Resume};
    if(v.page==Page::Player){
        if(l.cover.Contains(x,y))return {Action::Tape};
        if(l.favorite.Contains(x,y))return {Action::Favorite};
        if(l.lyrics.Contains(x,y))return {Action::PageLyrics};
        if(l.queue.Contains(x,y))return {Action::PageQueue};
        if(l.previous.Contains(x,y))return {Action::Previous};
        if(l.toggle.Contains(x,y))return {Action::Toggle,v.current};
        if(l.next.Contains(x,y))return {Action::Next};
        if(l.seek.Contains(x,y)&&v.duration>0&&std::isfinite(v.duration)&&v.seekable)return {Action::Seek,-1,l.SeekFraction(x)};
        return {};
    }
    if(MiniPrevious.Contains(x,y))return {Action::Previous};
    if(MiniToggle.Contains(x,y))return {Action::Toggle,v.current};
    if(MiniNext.Contains(x,y))return {Action::Next};
    if(MiniQueue.Contains(x,y))return {Action::PageQueue};
    if(Within(x,y,40,608,740,64))return {Action::PagePlayer};
    if(v.page==Page::Library){
        for(int i=0;i<4;++i)if(SourceTab(i).Contains(x,y))return {sourceActions[i]};
        if(v.librarySource!=LibrarySource::Playlists)for(int i=0;i<3;++i)if(Within(x,y,256+i*108,176,96,56))return {i==0?Action::GroupSongs:i==1?Action::GroupAlbums:Action::GroupArtists};
        if(Within(x,y,652,176,588,56))return {Action::Search};
        if((!v.query.empty()||!v.subtitle.empty())&&Within(x,y,916,120,132,64))return {Action::Back};
        if(Within(x,y,1064,120,176,64)){
            if(v.librarySource==LibrarySource::Playlists&&v.showingGroups&&v.groups&&!v.groups->empty())return {Action::DeletePlaylist,v.selected};
            if(!v.showingGroups&&v.tracks&&!v.tracks->empty())return {Action::Enqueue,v.selected};
        }
        if(LibraryListRect().Contains(x,y)){
            int local=y-ListTop-v.scrollPixels,pitch=v.showingGroups?GroupRowHeight:ListRowHeight,row=int(std::floor(local/double(pitch))),inside=local-row*pitch;
            if(v.showingGroups&&v.groups){int col=(x-256)/500,i=v.offset+row*GroupColumns+col;
                if(col<GroupColumns&&(x-256)%500<476&&inside<148&&i>=0&&size_t(i)<v.groups->size())return {Action::GroupItem,i};
            }else{int i=v.offset+row;
            if(x<1228&&inside<76&&i>=0&&v.tracks&&size_t(i)<v.tracks->size())return {Action::Song,i};}
        }
    }else if(v.page==Page::Queue){
        if(Within(x,y,40,120,136,64))return {Action::PagePlayer};
        bool selected=v.tracks&&v.selected>=0&&size_t(v.selected)<v.tracks->size();
        if(selected){if(Within(x,y,680,144,140,64))return {Action::QueueSong,v.selected};
        if(Within(x,y,832,144,120,64))return {Action::QueueUp,v.selected};
        if(Within(x,y,964,144,120,64))return {Action::QueueDown,v.selected};
        if(Within(x,y,1096,144,144,64))return {Action::QueueRemove,v.selected};}
        if(Within(x,y,40,ListTop,1188,ListBottom-ListTop)){int local=y-ListTop-v.scrollPixels,row=int(std::floor(local/double(ListRowHeight))),i=v.offset+row;
        if(local-row*ListRowHeight<76&&i>=0&&v.tracks&&size_t(i)<v.tracks->size())return {Action::QueueSelect,i};}
    }else if(v.page==Page::Lyrics){
        if(Within(x,y,40,120,136,64))return {Action::PagePlayer};
        if(Within(x,y,800,136,140,64))return {Action::LyricEarlier};
        if(Within(x,y,950,136,140,64))return {Action::LyricReset};
        if(Within(x,y,1100,136,140,64))return {Action::LyricLater};
    }else if(v.page==Page::Settings){
        for(int i=0;i<4;++i)if(SettingsTab(i).Contains(x,y))return {settingsActions[i]};
        if(v.settingsSection==SettingsSection::Playback){
            if(VolumeMinus.Contains(x,y))return {Action::VolumeDown};
            if(VolumePlus.Contains(x,y))return {Action::VolumeUp};
            if(ModeRow.Contains(x,y))return {Action::Mode};
            for(int i=0;i<3;++i)if(ThemeChoice(i).Contains(x,y))return {Action::CycleTheme,i};
        }else if(v.settingsSection==SettingsSection::Storage){
            if(CacheMinus.Contains(x,y))return {Action::CacheDown};
            if(CachePlus.Contains(x,y))return {Action::CacheUp};
            if(ClearCacheRow.Contains(x,y))return {Action::ClearCache};
            if(RefreshLibrary.Contains(x,y))return {Action::Refresh};
        }else if(v.settingsSection==SettingsSection::Playlists){
            for(int i=0;i<3;++i)if(PlaylistRow(i).Contains(x,y))return {playlistActions[i]};
        }else if(v.settingsSection==SettingsSection::Sources){
            if(SourceAddButton.Contains(x,y))return {Action::SourceAdd};
            if(SourceRefreshButton.Contains(x,y))return {Action::Refresh};
            bool selected=v.sources&&v.selected>=0&&size_t(v.selected)<v.sources->size();
            if(selected){if(SourceEditButton.Contains(x,y))return {Action::SourceEdit,v.selected};
            if(SourceDeleteButton.Contains(x,y))return {Action::SourceDelete,v.selected};}
            if(SourceListRect().Contains(x,y)){int local=y-SourceListTop-v.scrollPixels,row=int(std::floor(local/double(SourceRowHeight))),i=v.offset+row;
                if(x<1228&&local-row*SourceRowHeight<76&&v.sources&&i>=0&&size_t(i)<v.sources->size())return {Action::SourceEdit,i};}
        }
    }
    return {};
}
