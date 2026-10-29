#include "app_model.hpp"
#include <cstdio>
using namespace papa;
#define CHECK(x)do{if(!(x)){std::fprintf(stderr,"FAIL app model line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    Track a{"sdmc:/music/A.mp3","晴天","周杰伦"};a.id="sd:a";a.album="叶惠美";
    Track b{"sdmc:/music/B.mp3","七里香","周杰伦"};b.id="sd:b";b.album="七里香";
    Track c{"https://example.test/c.mp3","夜曲","周杰伦"};c.id="cloud:main:c";c.album="十一月的萧邦";
    LibraryModel m;m.SetTracks(SourceKind::SD,{a,b});m.SetTracks(SourceKind::Cloud,{c});
    CHECK(m.Visible().size()==2);CHECK(m.StartVisible(0));CHECK(m.Current()->id==a.id);
    m.SelectSource(LibrarySource::Cloud);CHECK(m.Visible().size()==1);CHECK(m.Current()->id==a.id);
    m.Enqueue(c);CHECK(m.Queued().size()==3);CHECK(m.StartQueue(2));CHECK(m.Current()->id==c.id);
    m.SetTracks(SourceKind::Cloud,{});CHECK(m.Current()->id==c.id);
    CHECK(m.ToggleFavorite(a));m.SelectSource(LibrarySource::Favorites);CHECK(m.Visible().size()==1);
    CHECK(m.Favorite(a));CHECK(!m.ToggleFavorite(a));CHECK(m.Visible().empty());
    m.SelectSource(LibrarySource::SD);m.query="qt";CHECK(m.Visible().size()==1);CHECK(m.Visible()[0].id==a.id);m.query.clear();
    m.SelectGrouping(Grouping::Albums);CHECK(m.ShowingGroups());CHECK(m.Groups().size()==2);
    m.groupKey=m.Groups()[0].key;CHECK(!m.ShowingGroups());CHECK(m.Visible().size()==1);
    std::string key=m.SaveQueueAsPlaylist("我的歌单");CHECK(!key.empty());CHECK(m.user.playlists.size()==1);
    m.SelectSource(LibrarySource::Playlists);CHECK(m.Groups().size()==1);m.groupKey=key;CHECK(m.Visible().size()==3);
    CHECK(m.MoveQueued(2,-1));CHECK(m.QueueIndex()==1);CHECK(m.Current()->id==c.id);
    CHECK(m.RemoveQueued(0));CHECK(m.Current()->id==c.id);CHECK(m.QueueIndex()==0);
    CHECK(m.RemoveQueued(0));CHECK(m.Current()->id==b.id);CHECK(m.RemoveQueued(0));CHECK(!m.Current());
    CHECK(!m.Next());CHECK(!m.StartQueue(4));
    std::puts("PASS app model: SD/cloud isolation, persistent queue, search/groups/favorites, mixed playlists, queue edit");
}
