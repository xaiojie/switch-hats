#pragma once
#include "core.hpp"
#include "app_model.hpp"
#include "assets.hpp"
#include <memory>
namespace papa {
constexpr int ScreenWidth=1280, ScreenHeight=720, VisibleRows=6;
enum class Page {Library, Player, Lyrics, Queue, Settings};
struct View {
    const std::vector<Track>* tracks=nullptr;
    int current=-1, selected=0, offset=0, volume=70;
    Mode mode=Mode::LoopAll;
    bool paused=true, scanning=false, loading=false, ended=false;
    double seconds=0, duration=0, animation=0;
    int sourceRate=0;
    std::string error;
    Page page=Page::Player;
    LibrarySource librarySource=LibrarySource::SD;
    Grouping grouping=Grouping::Songs;
    const Track* playing=nullptr;
    const std::vector<LibraryGroup>* groups=nullptr;
    const std::vector<MediaSource>* sources=nullptr;
    const Lyrics* lyricData=nullptr;
    const CoverImage* coverImage=nullptr;
    std::vector<std::shared_ptr<CoverImage>> thumbnails;
    bool showingGroups=false,tape=true,favorite=false,buffering=false,seekable=true,durationKnown=true;
    bool stale=false,resumeAvailable=false;
    int lyricScroll=-1;
    double lyricOffset=0;
    uint64_t cacheBytes=0,cacheLimit=512ull*1024*1024,networkBytes=0,networkTotal=0;
    std::string query,notice,subtitle;
};
enum class Action {
    None, Song, Toggle, Previous, Next, Seek, Mode, VolumeDown, VolumeUp,
    PageLibrary,PagePlayer,PageLyrics,PageQueue,PageSettings,
    SourceSD,SourceCloud,SourceFavorites,SourcePlaylists,GroupSongs,GroupAlbums,GroupArtists,GroupItem,
    Search,Back,Enqueue,Favorite,Tape,Refresh,SourceAdd,SourceEdit,SourceDelete,
    CacheDown,CacheUp,ClearCache,Resume,SavePlaylist,ImportPlaylist,ExportPlaylist,DeletePlaylist,
    QueueSong,QueueRemove,QueueUp,QueueDown,LyricEarlier,LyricLater,LyricReset
};
struct Hit {Action action=Action::None; int index=-1; double fraction=0;};
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
