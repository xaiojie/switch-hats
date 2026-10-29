#pragma once
#include "media.hpp"
namespace papa {
enum class LibrarySource { SD, Cloud, Favorites, Playlists };
enum class Grouping { Songs, Albums, Artists };
struct LibraryGroup {std::string key,title,subtitle;size_t count=0;};
class LibraryModel {
public:
    MediaState user;
    LibrarySource source=LibrarySource::SD;
    Grouping grouping=Grouping::Songs;
    std::string query,groupKey;
    void SetTracks(SourceKind kind,std::vector<Track> tracks);
    std::vector<Track> Visible() const;
    std::vector<LibraryGroup> Groups() const;
    void SelectSource(LibrarySource value);
    void SelectGrouping(Grouping value);
    bool ShowingGroups() const;
    bool StartVisible(size_t index);
    bool StartQueue(size_t index);
    bool Next(bool automatic=false);
    bool Previous();
    void Enqueue(const Track& track);
    bool RemoveQueued(size_t index);
    bool MoveQueued(size_t index,int delta);
    bool ToggleFavorite(const Track& track);
    bool Favorite(const Track& track) const;
    std::string SaveQueueAsPlaylist(const std::string& name);
    void SetMode(Mode value);
    Mode GetMode() const{return order_.GetMode();}
    const Track* Current() const;
    const std::vector<Track>& Queued() const{return queued_;}
    int QueueIndex() const{return order_.Current();}
    const std::vector<Track>& Local()const{return local_;}
    const std::vector<Track>& Cloud()const{return cloud_;}
private:
    std::vector<Track> Base() const;
    std::vector<Track> local_,cloud_,queued_;
    Queue order_{static_cast<uint32_t>(527)};
};
}
