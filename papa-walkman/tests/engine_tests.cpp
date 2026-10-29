#include "engine.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <vector>
using namespace papa;
struct Sink:AudioSink{
    std::deque<std::vector<int16_t>>queue;bool paused=false;uint64_t consumed=0,total=0;int resets=0;bool fail=false;
    bool Reset(std::string&)override{queue.clear();consumed=0;paused=false;++resets;return true;}
    bool Pause(bool p,std::string&)override{paused=p;return true;}
    bool Poll(uint64_t& frames,std::string&)override{frames=consumed;consumed=0;return true;}
    bool Room()const override{return queue.size()<3;}
    bool Empty()const override{return queue.empty();}
    bool Submit(const int16_t*s,size_t n,std::string&e)override{if(fail){e="output failure";return false;}queue.emplace_back(s,s+n*2);return true;}
    void Consume(){if(!paused&&!queue.empty()){consumed+=queue.front().size()/2;total+=queue.front().size()/2;queue.pop_front();}}
};
#define CHECK(x)do{if(!(x)){std::fprintf(stderr,"FAIL engine line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
    CHECK(argc==2);Sink s;Engine e(s);CHECK(e.Open(argv[1]));e.Tick();CHECK(s.queue.size()==3);
    CHECK(e.State().seconds==0);CHECK(!e.State().ended);e.Pause(true);auto front=s.queue.front();
    s.Consume();e.Tick();CHECK(s.queue.size()==3);CHECK(s.queue.front()==front);CHECK(e.State().seconds==0);
    e.Pause(false);s.Consume();e.Tick();CHECK(e.State().seconds>0);CHECK(s.queue.size()==3);
    int oldResets=s.resets;e.Seek(0.8);CHECK(s.resets==oldResets+1);CHECK(s.queue.empty());CHECK(e.State().seconds==0.8);
    e.Tick();CHECK(s.queue.size()==3);CHECK(e.State().seconds==0.8);
    e.Volume(900);CHECK(e.State().volume==100);e.Volume(-10);CHECK(e.State().volume==0);
    int guard=0;while(!e.State().ended&&guard++<1000){s.Consume();e.Tick();}
    CHECK(guard<1000);CHECK(s.Empty());CHECK(e.State().seconds>=e.State().duration-0.03);
    uint64_t firstCompletion=e.State().completion;CHECK(firstCompletion>0);
    e.Seek(0.8);CHECK(!e.State().ended);guard=0;
    while(!e.State().ended&&guard++<1000){s.Consume();e.Tick();}
    CHECK(guard<1000);CHECK(e.State().completion>firstCompletion);
    CHECK(e.Open(argv[1]));CHECK(!e.State().ended);e.Tick();CHECK(s.queue.size()==3);
    CHECK(!e.Open("missing.mp3"));CHECK(!e.State().error.empty());CHECK(s.queue.empty());
    CHECK(e.Open(argv[1]));s.fail=true;e.Tick();CHECK(e.State().error=="output failure");
    std::puts("PASS engine: real decode/output queue, pause ownership, progress, seek reset, EOF drain, output errors");
}
