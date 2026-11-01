#pragma once
#include "core.hpp"
#include "app_model.hpp"
#include "assets.hpp"
#include <memory>
namespace papa {
constexpr int ScreenWidth=1280, ScreenHeight=720;
constexpr int ListTop=240, ListBottom=560, ListRowHeight=80, ListVisibleRows=4;
constexpr int GroupRowHeight=160, GroupColumns=2;
constexpr int SourceListTop=240, SourceListBottom=480, SourceRowHeight=80, SourceVisibleRows=3;
constexpr int LyricListTop=224, LyricListBottom=576, LyricRowHeight=64;
constexpr int VisibleRows=ListVisibleRows;
enum class Page {Library, Player, Lyrics, Queue, Settings};
enum class SettingsSection {Playback, Sources, Storage, Playlists};
struct View {
    const std::vector<Track>* tracks=nullptr;
    int current=-1, selected=0, offset=0, volume=70;
    Mode mode=Mode::LoopAll;
    Theme theme=Theme::AlbumLiner;
    bool paused=true, scanning=false, loading=false, ended=false;
    double seconds=0, duration=0, animation=0;
    int sourceRate=0;
    std::string error;
    Page page=Page::Player;
    SettingsSection settingsSection=SettingsSection::Playback;
    LibrarySource librarySource=LibrarySource::SD;
    Grouping grouping=Grouping::Songs;
    const Track* playing=nullptr;
    const std::vector<LibraryGroup>* groups=nullptr;
    const std::vector<MediaSource>* sources=nullptr;
    const Lyrics* lyricData=nullptr;
    const CoverImage* coverImage=nullptr;
    std::vector<std::shared_ptr<CoverImage>> thumbnails;
    bool showingGroups=false,tape=false,favorite=false,buffering=false,seekable=true,durationKnown=true;
    bool stale=false,resumeAvailable=false;
    bool artLoading=false,seeking=false;
    int scrollPixels=0,touchX=-1,touchY=-1;
    double seekFraction=0;
    int lyricScroll=-1;
    double lyricOffset=0;
    double lyricPosition=-1;
    bool lyricAnimating=false;
    uint64_t cacheBytes=0,cacheLimit=512ull*1024*1024,networkBytes=0,networkTotal=0;
    std::string query,notice,subtitle;
};
enum class Action {
    None, Song, Toggle, Previous, Next, Seek, Mode, VolumeDown, VolumeUp,
    PageLibrary,PagePlayer,PageLyrics,PageQueue,PageSettings,
    SourceSD,SourceCloud,SourceFavorites,SourcePlaylists,GroupSongs,GroupAlbums,GroupArtists,GroupItem,
    Search,Back,Enqueue,Favorite,Tape,Refresh,SourceAdd,SourceEdit,SourceDelete,
    CacheDown,CacheUp,ClearCache,Resume,SavePlaylist,ImportPlaylist,ExportPlaylist,DeletePlaylist,
    QueueSong,QueueRemove,QueueUp,QueueDown,LyricEarlier,LyricLater,LyricReset,
    CycleSource,CycleGrouping,QueueSelect,CycleTheme,
    SettingsPlayback,SettingsSources,SettingsStorage,SettingsPlaylists
};
// Drawing, taps and drag seeking use the same physical geometry. Bounds are half-open.
struct Rect {
    int x=0,y=0,width=0,height=0;
    bool Contains(int px,int py)const{return px>=x&&px<x+width&&py>=y&&py<y+height;}
};
struct PlayerLayout {
    Rect libraryTab,playerTab,settingsTab,cover,lyrics,favorite,queue,seek,previous,toggle,next;
    double SeekFraction(int x)const;
};
PlayerLayout GetPlayerLayout(Theme theme);
struct Hit {Action action=Action::None; int index=-1; double fraction=0;};
Rect LibraryListRect();
Rect SourceListRect();
Hit SettingAction(const View& view);
int SettingCount(SettingsSection section);
struct PointerGesture {Hit hit;int scrollRows=0;bool scrolling=false,lyrics=false;};
Hit HitTest(int x,int y,const View& view);
PointerGesture ResolveGesture(int startX,int startY,int endX,int endY,const View& view);
std::vector<std::string> WrapPlainLyrics(const std::string& text,size_t columns=66);
int ClampLyricScroll(const Lyrics* lyrics,int requested);
class Renderer {
public:
    Renderer(); ~Renderer();
    bool AddFont(const void* bytes, size_t size);
    void Draw(uint32_t* pixels,int strideBytes,const View& view);
private:
    struct Impl; std::unique_ptr<Impl> p_;
};
}
