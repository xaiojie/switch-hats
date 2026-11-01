#pragma once
#include "ui.hpp"
namespace papa {
Page NextMainPage(Page page);
Page ParentPage(Page page);
Hit ConfirmAction(const View& view);
Hit SecondaryAction(const View& view);
bool NeedsLibraryRebuild(Action action);
struct TouchResult {
 Hit action{};
 bool scrolling=false,lyrics=false,preview=false;
 int deltaY=0;
 double fraction=0;
};
// One consumer. Drags update continuously; a seek commits only on release.
class TouchInput {
public:
 TouchResult Update(bool down,int x,int y,const View& view);
 bool Held()const{return held_;}
 bool Dragging()const{return moved_;}
 int X()const{return x_;}int Y()const{return y_;}
 void Cancel(){blocked_=blocked_||held_;held_=false;}
private:
 bool held_=false,moved_=false,seek_=false,scroll_=false,lyrics_=false,blocked_=false;
 int x_=0,y_=0,startX_=0,startY_=0,outputY_=0;
 Page page_=Page::Library;
 Theme theme_=Theme::AlbumLiner;
 SettingsSection settingsSection_=SettingsSection::Playback;
 LibrarySource librarySource_=LibrarySource::SD;
 Grouping grouping_=Grouping::Songs;
 bool showingGroups_=false;
 std::string query_,subtitle_;
 std::string trackId_,trackPath_;
 Hit pressed_{};
 double fraction_=0;
};
}
