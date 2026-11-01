#include "interaction.hpp"
#include <cstdio>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL interaction %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 CHECK(NextMainPage(Page::Library)==Page::Player);
 CHECK(NextMainPage(Page::Player)==Page::Settings);
 CHECK(NextMainPage(Page::Settings)==Page::Library);
 CHECK(ParentPage(Page::Lyrics)==Page::Player);
 CHECK(ParentPage(Page::Queue)==Page::Player);
 View view;view.page=Page::Player;view.selected=3;
 CHECK(ConfirmAction(view).action==Action::Toggle);
 view.page=Page::Lyrics;CHECK(ConfirmAction(view).action==Action::Toggle);
 view.page=Page::Queue;CHECK(ConfirmAction(view).action==Action::QueueSong);
 CHECK(ConfirmAction(view).index==3);
 CHECK(!NeedsLibraryRebuild(Action::Toggle));CHECK(!NeedsLibraryRebuild(Action::Seek));
 CHECK(!NeedsLibraryRebuild(Action::None));CHECK(!NeedsLibraryRebuild(Action::VolumeUp));
 CHECK(NeedsLibraryRebuild(Action::QueueRemove));CHECK(NeedsLibraryRebuild(Action::Favorite));
 view.page=Page::Library;view.librarySource=LibrarySource::Playlists;view.showingGroups=false;
 CHECK(SecondaryAction(view).action==Action::Favorite);
 view.showingGroups=true;CHECK(SecondaryAction(view).action==Action::DeletePlaylist);
 view.librarySource=LibrarySource::Cloud;CHECK(SecondaryAction(view).action==Action::Favorite);
 view.page=Page::Player;CHECK(SecondaryAction(view).action==Action::PageLyrics);
 view.page=Page::Lyrics;CHECK(SecondaryAction(view).action==Action::PagePlayer);
 view.page=Page::Settings;view.settingsSection=SettingsSection::Sources;CHECK(SecondaryAction(view).action==Action::Refresh);
 view.showingGroups=false;view.librarySource=LibrarySource::SD;
 view.page=Page::Library;std::vector<Track> tracks(20);view.tracks=&tracks;
 TouchInput touch;CHECK(touch.Update(true,500,330,view).action.action==Action::None);
 auto drag=touch.Update(true,500,310,view);CHECK(drag.scrolling);CHECK(drag.deltaY==-20);
 drag=touch.Update(true,500,302,view);CHECK(drag.deltaY==-8);
 auto released=touch.Update(false,500,302,view);CHECK(released.action.action==Action::None);
 CHECK(!touch.Held());
 CHECK(!NeedsLibraryRebuild(Action::CycleTheme));
 Track first;first.id="first";first.path="first.mp3";Track next=first;next.id="next";next.path="next.mp3";
 for(auto theme:{Theme::AlbumLiner,Theme::NightRecord,Theme::LyricTheatre}){
  view.page=Page::Player;view.theme=theme;view.duration=200;view.seekable=true;view.playing=&first;
  auto l=GetPlayerLayout(theme);int sx=l.seek.x+10,sy=l.seek.y+20,end=l.seek.x+l.seek.width-1;
  CHECK(touch.Update(true,sx,sy,view).preview);
  auto seek=touch.Update(true,end,sy,view);CHECK(seek.preview);CHECK(seek.action.action==Action::None);CHECK(seek.fraction==1);
  released=touch.Update(false,end,sy,view);CHECK(released.action.action==Action::Seek);CHECK(released.action.fraction==1);
  const int tx=l.toggle.x+l.toggle.width/2,ty=l.toggle.y+l.toggle.height/2;
  touch.Update(true,tx,ty,view);view.page=Page::Library;
  CHECK(touch.Update(false,tx,ty,view).action.action==Action::None);
  view.page=Page::Player;touch.Update(true,tx,ty,view);touch.Cancel();
  CHECK(touch.Update(true,tx,ty,view).action.action==Action::None);CHECK(!touch.Held());
  CHECK(touch.Update(false,tx,ty,view).action.action==Action::None);
  touch.Update(true,tx,ty,view);CHECK(touch.Update(false,tx,ty,view).action.action==Action::Toggle);
  CHECK(touch.Update(true,sx,sy,view).preview);view.playing=&next;
  CHECK(!touch.Update(true,end,sy,view).preview);CHECK(touch.Update(false,end,sy,view).action.action==Action::None);
  view.playing=&first;
  CHECK(touch.Update(true,sx,sy,view).preview);view.theme=NextTheme(theme);
  CHECK(!touch.Update(true,end,sy,view).preview);CHECK(!touch.Held());
  CHECK(touch.Update(false,end,sy,view).action.action==Action::None);
  view.theme=theme;touch.Update(true,tx,ty,view);view.theme=NextTheme(theme);
  CHECK(touch.Update(false,tx,ty,view).action.action==Action::None);
 }
 // Settings theme presses also cancel before release if a controller switched themes.
 view.page=Page::Settings;view.settingsSection=SettingsSection::Playback;touch.Update(true,580,470,view);view.theme=NextTheme(view.theme);
 CHECK(touch.Update(false,580,470,view).action.action==Action::None);
 touch.Update(true,580,470,view);CHECK(touch.Update(false,580,470,view).action.action==Action::CycleTheme);
 // A released finger must never activate a different setting or a newly filtered song.
 view.settingsSection=SettingsSection::Playback;touch.Update(true,600,340,view);view.settingsSection=SettingsSection::Storage;
 CHECK(touch.Update(false,600,340,view).action.action==Action::None);
 view.page=Page::Library;view.librarySource=LibrarySource::SD;view.grouping=Grouping::Songs;view.showingGroups=false;view.query.clear();view.subtitle.clear();
 touch.Update(true,500,270,view);view.librarySource=LibrarySource::Cloud;
 CHECK(touch.Update(false,500,270,view).action.action==Action::None);
 touch.Update(true,500,270,view);view.query="new search";
 CHECK(touch.Update(false,500,270,view).action.action==Action::None);
 touch.Update(true,500,270,view);view.grouping=Grouping::Albums;view.showingGroups=true;
 CHECK(touch.Update(false,500,270,view).action.action==Action::None);
 view.showingGroups=false;touch.Update(true,500,270,view);view.subtitle="different playlist";
 CHECK(touch.Update(false,500,270,view).action.action==Action::None);
 view.page=Page::Settings;view.settingsSection=SettingsSection::Playback;
 view.selected=5;CHECK(ConfirmAction(view).action==Action::CycleTheme&&ConfirmAction(view).index==2);
 view.settingsSection=SettingsSection::Playlists;view.selected=1;CHECK(ConfirmAction(view).action==Action::SavePlaylist);
 view.settingsSection=SettingsSection::Sources;view.sources=nullptr;view.selected=0;CHECK(ConfirmAction(view).action==Action::SourceAdd);
 std::puts("PASS interaction: primary/child navigation, confirm semantics, rebuild policy, live drag, single seek commit, stale-page, track and theme cancellation");
}
