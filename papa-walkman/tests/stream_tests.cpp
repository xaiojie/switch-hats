#include "stream.hpp"
#include "decoder.hpp"
#include "engine.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using namespace papa;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Sink:AudioSink {
 uint64_t queued=0;bool paused=false;
 bool Reset(std::string&)override{queued=0;return true;}bool Pause(bool p,std::string&)override{paused=p;return true;}
 bool Poll(uint64_t& played,std::string&)override{played=paused?0:queued;if(!paused)queued=0;return true;}
 bool Room()const override{return queued<4096;}bool Empty()const override{return !queued;}
 bool Submit(const int16_t*,size_t frames,std::string&)override{queued+=frames;return true;}
};
int review(const std::string& base,const std::string& mode){
 unsigned char b[8192];
 if(mode=="long") {Decoder d;CHECK(d.Open("build-host/stream/long.mp3"));CHECK(d.Duration()>60);size_t frames=0,n;std::vector<int16_t> pcm(4096);while((n=d.Read(pcm.data(),2048)))frames+=n;Decoder one;CHECK(one.Open("tests/tone.mp3"));size_t expected=0;while((n=one.Read(pcm.data(),2048)))expected+=n;CHECK(frames==expected*32);}
 if(mode=="fragment") {Decoder d;auto status=std::make_shared<StreamStatus>();CHECK(d.Open(base+"/fragment",nullptr,0,status));std::vector<int16_t> pcm(4096);CHECK(d.Read(pcm.data(),2048)==2048);CHECK(!status->Snapshot().eof);size_t frames=2048,n;while((n=d.Read(pcm.data(),2048)))frames+=n;Decoder local;CHECK(local.Open("build-host/stream/long.mp3"));size_t expected=0;while((n=local.Read(pcm.data(),2048)))expected+=n;CHECK(frames==expected);CHECK(d.Error().empty());}
 if(mode=="late") {Sink sink;Engine e(sink);CHECK(e.Open(base+"/late"));CHECK(e.State().durationKnown);for(int i=0;i<2000&&!e.State().ended&&e.State().error.empty();++i)e.Tick();CHECK(!e.State().error.empty());CHECK(!e.State().ended);CHECK(e.State().completion==0);}
 if(mode=="late-cancel") {std::atomic<uint64_t> token{1};Decoder d;auto status=std::make_shared<StreamStatus>();CHECK(d.Open(base+"/late-cancel",&token,1,status));std::thread reader([&](){int16_t pcm[4096];while(d.Read(pcm,2048)){};});std::this_thread::sleep_for(std::chrono::milliseconds(150));auto start=std::chrono::steady_clock::now();token=2;reader.join();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500));CHECK(!d.Error().empty());CHECK(!status->Snapshot().eof);d.Close();}
 if(mode=="range-short") {for(auto path:{"/range-short","/range-chunk-short"}){ByteSource s;CHECK(s.Open(base+path));while(s.Read(b,sizeof b)){}CHECK(!s.Status().error.empty());CHECK(!s.Status().eof);}}
 if(mode=="seek") {ByteSource s;CHECK(s.Open(base+"/fallback"));CHECK(s.Read(b,128)>0);CHECK(!s.Seek(s.Status().totalBytes+1));CHECK(s.Seek(s.Status().totalBytes));CHECK(s.Read(b,1)==0);CHECK(s.Status().error.empty());s.Close();CHECK(s.Open(base+"/ignored"));while(s.Read(b,sizeof b)){}auto length=s.Status().totalBytes;CHECK(length>0);CHECK(!s.Seek(length+1));CHECK(s.Seek(length));CHECK(s.Read(b,1)==0);}
 if(mode=="lifetime") {
#ifdef _WIN32
  // A protected page makes a detached worker dereference of the retired token fatal.
  for(int i=0;i<40;++i){void* page=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(page);auto token=new(page)std::atomic<uint64_t>(1);ByteSource s;CHECK(s.Open(base+"/stalled",token,1));std::this_thread::sleep_for(std::chrono::milliseconds(3));s.Close();token->~atomic();DWORD old;CHECK(VirtualProtect(page,4096,PAGE_NOACCESS,&old));std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(VirtualFree(page,0,MEM_RELEASE));}
#endif
 }
 std::printf("PASS review %s\n",mode.c_str());return 0;
}
int main(int argc,char**argv){
 if(argc==3)return review(argv[1],argv[2]);
 CHECK(argc==2);
 {FILE* f=OpenUtf8File(u8"build-host/stream/中文文件.tmp","w+b");CHECK(f);std::fputs("hello",f);std::fclose(f);ByteSource local;CHECK(local.Open(u8"build-host/stream/中文文件.tmp"));char c[5];CHECK(local.Read(c,5)==5);CHECK(c[0]=='h');}
 std::string base=argv[1];
 auto missing=FetchUrl(base+"/missing",100); CHECK(!missing.error.empty());
 auto large=FetchUrl(base+"/tone",16); CHECK(!large.error.empty());
 auto redir=FetchUrl(base+"/redirect",100000); CHECK(redir.error.empty());CHECK(redir.finalUrl==base+"/tone");
 auto status=std::make_shared<StreamStatus>(); std::atomic<uint64_t> cancel{1};
 ByteSource source; StreamOptions opts;opts.cacheBytes=65536;opts.prefetchBytes=32768;
 CHECK(source.Open(base+"/slow",&cancel,1,status,opts));
 unsigned char b[4096];CHECK(source.Read(b,sizeof b)>0);CHECK(!status->Snapshot().eof);
 CHECK(source.Seek(0));CHECK(source.Read(b,128)==128);
 cancel=2;CHECK(source.Read(b,sizeof b)==0);CHECK(status->Snapshot().cancelled);
 Decoder d;auto st=std::make_shared<StreamStatus>();auto start=std::chrono::steady_clock::now();
 CHECK(d.Open(base+"/slow",nullptr,0,st,opts));
 std::vector<int16_t> pcm(2048*2);CHECK(d.Read(pcm.data(),2048)>0);long long energy=0;for(auto sample:pcm)energy+=std::abs(int(sample));CHECK(energy>1000);CHECK(!st->Snapshot().eof);
 CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(3));d.Close();
 CHECK(d.Open(base+"/unknown"));CHECK(!d.DurationKnown());while(d.Read(pcm.data(),2048)){}CHECK(d.Error().empty());
 CHECK(!d.Open(base+"/missing"));
 ByteSource broken;CHECK(broken.Open(base+"/truncated"));while(broken.Read(b,sizeof b)){}CHECK(!broken.Status().error.empty());
 ByteSource ignored;CHECK(ignored.Open(base+"/ignored"));CHECK(ignored.Read(b,128)==128);CHECK(!ignored.Status().rangeSupported);CHECK(ignored.Seek(0));
 ByteSource range;CHECK(range.Open(base+"/tone"));CHECK(range.Read(b,128)==128);CHECK(range.Status().rangeSupported);
 ByteSource bad;CHECK(bad.Open(base+"/bad-range"));CHECK(bad.Read(b,128)==0);CHECK(!bad.Status().error.empty());
 StreamOptions seekOpts;seekOpts.cacheBytes=32768;seekOpts.prefetchBytes=16384;
 ByteSource off;CHECK(off.Open(base+"/tone",nullptr,0,{},seekOpts));CHECK(off.Read(b,128)==128);CHECK(off.Seek(30000));CHECK(off.Read(b,128)==128);CHECK(off.Status().error.empty());
 ByteSource changed;CHECK(changed.Open(base+"/changed",nullptr,0,{},seekOpts));CHECK(changed.Read(b,128)==128);CHECK(changed.Seek(30000));CHECK(changed.Read(b,128)==0);CHECK(changed.Status().error.find("changed")!=std::string::npos);
 ByteSource invalid;CHECK(invalid.Open(base+"/range416",nullptr,0,{},seekOpts));CHECK(invalid.Read(b,128)==128);CHECK(invalid.Seek(30000));CHECK(invalid.Read(b,128)==0);CHECK(invalid.Status().error.find("416")!=std::string::npos);
 ByteSource ring;CHECK(ring.Open(base+"/large",nullptr,0,{},seekOpts));size_t bytes=0;while(auto n=ring.Read(b,sizeof b))bytes+=n;CHECK(bytes>500000);CHECK(ring.Status().cachedStart>0);CHECK(ring.Status().cachedEnd-ring.Status().cachedStart<=32768);CHECK(ring.Status().rangeSupported);CHECK(ring.Seek(0));CHECK(ring.Read(b,128)==128);
 std::atomic<uint64_t> generation{7};ByteSource stalled;CHECK(stalled.Open(base+"/stalled",&generation,7));size_t received=999;std::thread reader([&](){received=stalled.Read(b,sizeof b);});std::this_thread::sleep_for(std::chrono::milliseconds(150));auto when=std::chrono::steady_clock::now();generation=8;reader.join();CHECK(received==0);CHECK(stalled.Status().cancelled);stalled.Close();CHECK(std::chrono::steady_clock::now()-when<std::chrono::milliseconds(500));auto fresh=FetchUrl(base+"/tone",100000);CHECK(fresh.error.empty());
 Sink sink;Engine engine(sink);CHECK(engine.Open(base+"/unknown"));CHECK(!engine.State().durationKnown);
 for(int i=0;i<200&&!engine.State().ended;++i)engine.Tick();CHECK(engine.State().ended);CHECK(engine.State().seconds>1.9&&engine.State().seconds<2.2);CHECK(engine.State().completion==1);CHECK(engine.State().error.empty());
 engine.Volume(62);engine.Stop();CHECK(engine.State().path.empty());CHECK(engine.State().paused);CHECK(engine.State().volume==62);CHECK(sink.Empty());engine.Tick();CHECK(!engine.State().ended);
 std::puts("PASS controlled HTTP startup, unknown duration, bounds, redirects, Range, truncation, 404, cancellation");
}
