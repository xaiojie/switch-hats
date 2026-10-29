#include "audio.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
using namespace papa;
struct Sink:AudioSink{
 std::atomic<int>submissions{0},resets{0};bool paused=true;int queued=0;
 bool Reset(std::string&)override{queued=0;paused=false;++resets;return true;}
 bool Pause(bool value,std::string&)override{paused=value;return true;}
 bool Poll(uint64_t&n,std::string&)override{n=0;if(!paused&&queued){--queued;n=1024;}return true;}
 bool Room()const override{return queued<3;}
 bool Empty()const override{return !queued;}
 bool Submit(const int16_t*,size_t,std::string&)override{++queued;++submissions;return true;}
};
template<class F>bool Until(F f){for(int i=0;i<500;++i){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(4));}return false;}
#define CHECK(x) do{if(!(x)){std::printf("FAIL audio %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){CHECK(argc==2);Sink sink;{
 AudioPlayer player(sink);player.Play(argv[1],.2,true);
 CHECK(Until([&]{auto s=player.Snapshot();return !s.loading&&s.seconds>=.2;}));CHECK(sink.submissions==0);CHECK(player.Snapshot().paused);
 player.Pause(false);CHECK(Until([&]{return sink.submissions>0;}));
 player.Pause(true);CHECK(Until([&]{return player.Snapshot().paused;}));
 std::this_thread::sleep_for(std::chrono::milliseconds(30));auto count=sink.submissions.load();std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(sink.submissions==count);
 player.Stop();CHECK(Until([&]{return player.Snapshot().path.empty();}));
 player.Play("definitely-missing-audio.mp3");CHECK(Until([&]{return !player.Snapshot().error.empty();}));CHECK(!player.Snapshot().loading);
 }CHECK(sink.resets>0);std::puts("PASS audio: injected sink worker, paused position resume, play/pause, stop, failed open");}
