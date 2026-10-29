#include "pause_gate.hpp"
#include <deque>
#include <cstdio>
using namespace papa;
struct Sink:AudioSink {
 std::deque<std::vector<int16_t>> blocks;bool paused=false;
 bool Reset(std::string&)override{blocks.clear();paused=false;return true;}
 bool Pause(bool p,std::string&)override{paused=p;return true;}
 bool Poll(uint64_t& n,std::string&)override{n=0;return true;}
 bool Room()const override{return blocks.size()<3;}
 bool Empty()const override{return blocks.empty();}
 bool Submit(const int16_t*p,size_t n,std::string&)override{blocks.emplace_back(p,p+2*n);return true;}
};
#define CHECK(x) do{if(!(x)){std::printf("FAIL gate %d\n",__LINE__);return 1;}}while(0)
int main(){Sink s;std::atomic<bool> paused{false};PauseGateSink g(s,paused);std::string e;uint64_t n;
 int16_t a[]={1,2,3,4},b[]={5,6,7,8};CHECK(g.Submit(a,2,e));
 paused=true;CHECK(g.Submit(b,2,e));CHECK(s.paused);CHECK(s.blocks.size()==1);CHECK(!g.Room());CHECK(!g.Empty());
 CHECK(g.Pause(false,e));CHECK(s.paused);CHECK(g.Poll(n,e));CHECK(s.blocks.size()==1);
 paused=false;CHECK(g.Pause(false,e));CHECK(s.blocks.size()==2);CHECK(s.blocks[0]==std::vector<int16_t>(a,a+4));CHECK(s.blocks[1]==std::vector<int16_t>(b,b+4));CHECK(!s.paused);
 paused=true;CHECK(g.Submit(a,2,e));CHECK(g.Reset(e));paused=false;CHECK(g.Poll(n,e));CHECK(s.blocks.empty());
 std::puts("PASS pause gate: real PCM retained in order; no paused submit; reset discards held block");}
