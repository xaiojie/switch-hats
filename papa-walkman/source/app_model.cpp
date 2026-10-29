#include "app_model.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace papa {
namespace {
std::string AlbumKey(const Track&t){return (t.albumArtist.empty()?t.artist:t.albumArtist)+"\n"+t.album;}
}
void LibraryModel::SetTracks(SourceKind kind,std::vector<Track> tracks){
    std::vector<Track>clean;std::set<std::string>ids;
    for(auto&t:tracks){t.source=kind;if(t.id.empty())t.id=(kind==SourceKind::SD?"sd:":"cloud:")+t.path;if(ids.insert(t.id).second)clean.push_back(std::move(t));}
    (kind==SourceKind::SD?local_:cloud_)=std::move(clean);
}
std::vector<Track>LibraryModel::Base()const{
    if(source==LibrarySource::SD)return local_;
    if(source==LibrarySource::Cloud)return cloud_;
    if(source==LibrarySource::Playlists){for(const auto&p:user.playlists)if(p.id==groupKey)return p.tracks;return {};}
    std::vector<Track>all=local_;all.insert(all.end(),cloud_.begin(),cloud_.end());
    std::set<std::string>included;std::vector<Track>favorites;
    for(const auto&t:all)if(user.favorites.count(t.id)&&included.insert(t.id).second)favorites.push_back(t);
    for(const auto&t:queued_)if(user.favorites.count(t.id)&&included.insert(t.id).second)favorites.push_back(t);
    return favorites;
}
std::vector<Track>LibraryModel::Visible()const{
    std::vector<Track>result;
    for(const auto&t:Base()){
        if(!MatchesTrack(t,query))continue;
        if(source!=LibrarySource::Playlists&&!groupKey.empty()){
            if(grouping==Grouping::Albums&&AlbumKey(t)!=groupKey)continue;
            if(grouping==Grouping::Artists&&t.artist!=groupKey)continue;
        }
        result.push_back(t);
    }
    if(grouping==Grouping::Albums&&!groupKey.empty()&&source!=LibrarySource::Playlists)
        std::stable_sort(result.begin(),result.end(),[](const Track&a,const Track&b){if(a.discNumber!=b.discNumber)return a.discNumber<b.discNumber;if(a.trackNumber!=b.trackNumber)return a.trackNumber<b.trackNumber;return a.title<b.title;});
    return result;
}
std::vector<LibraryGroup>LibraryModel::Groups()const{
    std::vector<LibraryGroup>result;
    if(source==LibrarySource::Playlists){for(const auto&p:user.playlists){Track t;t.title=p.name;if(MatchesTrack(t,query))result.push_back({p.id,p.name,"混合来源歌单",p.tracks.size()});}return result;}
    std::map<std::string,LibraryGroup>groups;
    for(const auto&t:Base()){
        if(!MatchesTrack(t,query))continue;
        auto key=grouping==Grouping::Albums?AlbumKey(t):t.artist;
        auto&g=groups[key];g.key=key;g.title=grouping==Grouping::Albums?t.album:t.artist;
        g.subtitle=grouping==Grouping::Albums?(t.albumArtist.empty()?t.artist:t.albumArtist):"歌手";++g.count;
    }
    for(auto&item:groups)result.push_back(std::move(item.second));
    return result;
}
void LibraryModel::SelectSource(LibrarySource value){source=value;groupKey.clear();query.clear();}
void LibraryModel::SelectGrouping(Grouping value){grouping=value;groupKey.clear();}
bool LibraryModel::ShowingGroups()const{return groupKey.empty()&&(source==LibrarySource::Playlists||grouping!=Grouping::Songs);}
bool LibraryModel::StartVisible(size_t index){
    auto tracks=Visible();if(index>=tracks.size())return false;
    queued_=std::move(tracks);order_.Reset(queued_.size(),static_cast<int>(index));return true;
}
bool LibraryModel::StartQueue(size_t index){if(index>=queued_.size())return false;order_.Select(static_cast<int>(index));return true;}
bool LibraryModel::Next(bool automatic){return order_.Next(automatic)>=0;}
bool LibraryModel::Previous(){return order_.Previous()>=0;}
void LibraryModel::Enqueue(const Track&track){
    if(track.path.empty())return;
    int current=order_.Current();queued_.push_back(track);order_.Reset(queued_.size(),current);
}
bool LibraryModel::RemoveQueued(size_t index){
    if(index>=queued_.size())return false;
    int current=order_.Current();queued_.erase(queued_.begin()+index);
    if(current>static_cast<int>(index))--current;
    if(current>=static_cast<int>(queued_.size()))current=static_cast<int>(queued_.size())-1;
    order_.Reset(queued_.size(),current);return true;
}
bool LibraryModel::MoveQueued(size_t index,int delta){
    int target=static_cast<int>(index)+delta;
    if(index>=queued_.size()||target<0||static_cast<size_t>(target)>=queued_.size())return false;
    int current=order_.Current();std::swap(queued_[index],queued_[target]);
    if(current==static_cast<int>(index))current=target;else if(current==target)current=static_cast<int>(index);
    order_.Reset(queued_.size(),current);return true;
}
bool LibraryModel::ToggleFavorite(const Track&track){if(track.id.empty())return false;if(user.favorites.erase(track.id))return false;user.favorites.insert(track.id);return true;}
bool LibraryModel::Favorite(const Track&track)const{return !track.id.empty()&&user.favorites.count(track.id)>0;}
std::string LibraryModel::SaveQueueAsPlaylist(const std::string&name){
    if(name.empty()||queued_.empty())return {};
    unsigned number=1;std::string id;
    for(;;++number){id="playlist-"+std::to_string(number);bool exists=false;for(const auto&p:user.playlists)if(p.id==id){exists=true;break;}if(!exists)break;}
    user.playlists.push_back({id,name,queued_});return id;
}
void LibraryModel::SetMode(Mode mode){order_.SetMode(mode);}
const Track*LibraryModel::Current()const{int i=order_.Current();return i>=0&&static_cast<size_t>(i)<queued_.size()?&queued_[i]:nullptr;}
}
