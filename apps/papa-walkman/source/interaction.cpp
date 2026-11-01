#include "interaction.hpp"
#include <cstdlib>
namespace papa {
Page NextMainPage(Page page){
 if(page==Page::Library)return Page::Player;
 if(page==Page::Settings)return Page::Library;
 return Page::Settings;
}
Page ParentPage(Page page){return page==Page::Lyrics||page==Page::Queue?Page::Player:Page::Library;}
Hit ConfirmAction(const View&view){
 if(view.page==Page::Library)return {view.showingGroups?Action::GroupItem:Action::Song,view.selected};
 if(view.page==Page::Queue)return {Action::QueueSong,view.selected};
 if(view.page==Page::Settings)return SettingAction(view);
 return {Action::Toggle};
}
Hit SecondaryAction(const View&view){
 if(view.page==Page::Library)return {view.librarySource==LibrarySource::Playlists&&view.showingGroups?Action::DeletePlaylist:Action::Favorite,view.selected};
 if(view.page==Page::Player)return {Action::PageLyrics};
 if(view.page==Page::Lyrics)return {Action::PagePlayer};
 if(view.page==Page::Settings&&(view.settingsSection==SettingsSection::Sources||view.settingsSection==SettingsSection::Storage))return {Action::Refresh};
 return {};
}
bool NeedsLibraryRebuild(Action action){
 switch(action){
 case Action::None:case Action::Toggle:case Action::Previous:case Action::Next:
 case Action::Seek:case Action::Mode:case Action::VolumeDown:case Action::VolumeUp:
 case Action::SettingsPlayback:case Action::SettingsSources:case Action::SettingsStorage:case Action::SettingsPlaylists:
 case Action::Tape:case Action::CycleTheme:case Action::QueueSelect:case Action::LyricEarlier:case Action::LyricLater:case Action::LyricReset:return false;
 default:return true;
 }
}
TouchResult TouchInput::Update(bool down,int x,int y,const View&view){
 TouchResult out;
 if(blocked_){if(!down)blocked_=false;return out;}
 bool changedTrack=held_&&seek_&&((view.playing?view.playing->id:std::string{})!=trackId_||(view.playing?view.playing->path:std::string{})!=trackPath_);
 bool changedContent=view.page==Page::Library&&(view.librarySource!=librarySource_||view.grouping!=grouping_||view.showingGroups!=showingGroups_||view.query!=query_||view.subtitle!=subtitle_);
 if(held_&&(view.page!=page_||view.theme!=theme_||view.settingsSection!=settingsSection_||changedContent||changedTrack)){held_=false;blocked_=down;return out;}
 if(down&&!held_){
  held_=true;moved_=false;startX_=x_=x;startY_=outputY_=y_=y;page_=view.page;theme_=view.theme;settingsSection_=view.settingsSection;
  librarySource_=view.librarySource;grouping_=view.grouping;showingGroups_=view.showingGroups;query_=view.query;subtitle_=view.subtitle;
  pressed_=HitTest(x,y,view);seek_=pressed_.action==Action::Seek;fraction_=pressed_.fraction;
  trackId_=view.playing?view.playing->id:std::string{};trackPath_=view.playing?view.playing->path:std::string{};
  auto gesture=ResolveGesture(x,y,x,y-100,view);scroll_=gesture.scrolling;lyrics_=gesture.lyrics;
 }
 if(!held_)return out;
 x_=x;y_=y;
 if(std::abs(x-startX_)>14||std::abs(y-startY_)>14)moved_=true;
 if(seek_){
  // Force the resolver's drag path; it uses the original seek track geometry
  // and clamps the current X even when the finger leaves the touch rectangle.
  auto gesture=ResolveGesture(startX_,startY_,x,startY_+100,view);
  if(gesture.hit.action==Action::Seek)fraction_=gesture.hit.fraction;
  out.preview=down;out.fraction=fraction_;
 }else if(down&&moved_&&scroll_){
  out.scrolling=true;out.lyrics=lyrics_;out.deltaY=y-outputY_;outputY_=y;
 }
 if(!down){
  if(seek_)out.action={Action::Seek,-1,fraction_};
  else if(!moved_){auto hit=HitTest(x,y,view);if(hit.action==pressed_.action&&hit.index==pressed_.index)out.action=hit;}
  held_=false;
 }
 return out;
}
}
