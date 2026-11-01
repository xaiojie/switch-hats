#include "stream.hpp"
#include "assets.hpp"
#include "decoder.hpp"
#include "engine.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <filesystem>
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
 if(mode=="prefetch") {
  StreamOptions options;options.cacheBytes=32768;options.prefetchBytes=16384;ByteSource source;
  CHECK(source.Open(base+"/tone",nullptr,0,{},options));
  for(int i=0;i<1000&&source.Status().cachedEnd<options.prefetchBytes;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(source.Status().cachedEnd==options.prefetchBytes);CHECK(source.Read(b,128)==128);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(source.Status().cachedEnd<=options.prefetchBytes+128);
 }
 if(mode=="shutdown") {
  const std::string directory="build-host/stream/shutdown-cache";
  std::filesystem::create_directories(directory);StreamOptions options;options.cacheDirectory=directory;
  auto token=std::make_unique<std::atomic<uint64_t>>(1);ByteSource source;
  CHECK(source.Open(base+"/stalled",token.get(),1,{},options));
  std::this_thread::sleep_for(std::chrono::milliseconds(150));source.Close();token.reset();
  ShutdownHttpWorkers();CHECK(std::filesystem::is_empty(directory));
  CHECK(FetchUrl(base+"/tone",100000).error.empty());ShutdownHttpWorkers();
 }
 if(mode=="long") {Decoder d;CHECK(d.Open("build-host/stream/long.mp3"));CHECK(d.Duration()>60);size_t frames=0,n;std::vector<int16_t> pcm(4096);while((n=d.Read(pcm.data(),2048)))frames+=n;Decoder one;CHECK(one.Open("tests/tone.mp3"));size_t expected=0;while((n=one.Read(pcm.data(),2048)))expected+=n;CHECK(frames==expected*32);}
 if(mode=="fragment") {Decoder d;auto status=std::make_shared<StreamStatus>();CHECK(d.Open(base+"/fragment",nullptr,0,status));std::vector<int16_t> pcm(4096);CHECK(d.Read(pcm.data(),2048)==2048);CHECK(!status->Snapshot().eof);size_t frames=2048,n;while((n=d.Read(pcm.data(),2048)))frames+=n;Decoder local;CHECK(local.Open("build-host/stream/long.mp3"));size_t expected=0;while((n=local.Read(pcm.data(),2048)))expected+=n;CHECK(frames==expected);CHECK(d.Error().empty());}
 if(mode=="late") {Sink sink;Engine e(sink);CHECK(e.Open(base+"/late"));CHECK(e.State().durationKnown);for(int i=0;i<2000&&!e.State().ended&&e.State().error.empty();++i)e.Tick();CHECK(!e.State().error.empty());CHECK(!e.State().ended);CHECK(e.State().completion==0);}
 if(mode=="late-cancel") {std::atomic<uint64_t> token{1};Decoder d;auto status=std::make_shared<StreamStatus>();CHECK(d.Open(base+"/late-cancel",&token,1,status));std::thread reader([&](){int16_t pcm[4096];while(d.Read(pcm,2048)){};});std::this_thread::sleep_for(std::chrono::milliseconds(150));auto start=std::chrono::steady_clock::now();token=2;reader.join();CHECK(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500));CHECK(!d.Error().empty());CHECK(!status->Snapshot().eof);d.Close();}
 if(mode=="range-short") {for(auto path:{"/range-short","/range-chunk-short"}){ByteSource s;CHECK(s.Open(base+path));while(s.Read(b,sizeof b)){}CHECK(!s.Status().error.empty());CHECK(!s.Status().eof);}}
 if(mode=="seek") {ByteSource s;CHECK(s.Open(base+"/fallback"));CHECK(s.Read(b,128)>0);CHECK(!s.Seek(s.Status().totalBytes+1));CHECK(s.Seek(s.Status().totalBytes));CHECK(s.Read(b,1)==0);CHECK(s.Status().error.empty());s.Close();CHECK(s.Open(base+"/ignored"));while(s.Read(b,sizeof b)){}auto length=s.Status().totalBytes;CHECK(length>0);CHECK(!s.Seek(length+1));CHECK(s.Seek(length));CHECK(s.Read(b,1)==0);}
 if(mode=="cached-resume") {
  auto before=FetchUrl(base+"/resume-count",100);CHECK(before.error.empty());int count=std::stoi(std::string(before.bytes.begin(),before.bytes.end()));
  std::atomic<uint64_t> token{1};auto status=std::make_shared<StreamStatus>();Sink sink;Engine engine(sink);
  CHECK(engine.Open(base+"/resume-unknown",&token,1,status));engine.Pause(true);CHECK(!engine.State().durationKnown);
  for(int i=0;i<1000&&!status->Snapshot().eof;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(status->Snapshot().eof);
  engine.Tick();CHECK(engine.State().paused);CHECK(engine.State().seconds==0);CHECK(engine.State().seekable);CHECK(!engine.State().durationKnown);
  engine.Seek(1);CHECK(engine.State().error.empty());CHECK(engine.State().paused);CHECK(engine.State().durationKnown);CHECK(engine.State().duration>1.9&&engine.State().duration<2.2);CHECK(engine.State().seconds==1);
  engine.Seek(1.5);CHECK(engine.State().seconds==1.5);engine.Seek(999);CHECK(engine.State().seconds==engine.State().duration);engine.Seek(1);
  engine.Pause(false);engine.Tick();engine.Tick();CHECK(engine.State().seconds>1);CHECK(!engine.State().ended);CHECK(engine.State().error.empty());engine.Stop();
  auto after=FetchUrl(base+"/resume-count",100);CHECK(after.error.empty());CHECK(std::stoi(std::string(after.bytes.begin(),after.bytes.end()))==count+1);
  StreamOptions options;options.prefetchBytes=32*1024*1024;
  auto mediumStatus=std::make_shared<StreamStatus>();Sink mediumSink;Engine medium(mediumSink);CHECK(medium.Open(base+"/resume-medium",nullptr,0,mediumStatus,options));medium.Pause(true);for(int i=0;i<2000&&!mediumStatus->Snapshot().eof;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(mediumStatus->Snapshot().eof);medium.Tick();CHECK(medium.State().seekable);medium.Seek(1);CHECK(medium.State().durationKnown);CHECK(medium.State().duration>60);CHECK(medium.State().seconds==1);medium.Stop();
  StreamOptions largeOptions;largeOptions.prefetchBytes=64*1024*1024;auto oversizedStatus=std::make_shared<StreamStatus>();Decoder oversized;CHECK(oversized.Open(base+"/resume-oversize",nullptr,0,oversizedStatus,largeOptions));for(int i=0;i<4000&&!oversizedStatus->Snapshot().eof;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(oversizedStatus->Snapshot().eof);CHECK(!oversized.Seekable());CHECK(!oversized.Seek(1));CHECK(oversized.Error().find("32 MiB")!=std::string::npos);CHECK(!oversized.DurationKnown());oversized.Close();
  Decoder frameLimited;auto frameStatus=std::make_shared<StreamStatus>();CHECK(frameLimited.Open(base+"/resume-frame-limit",nullptr,0,frameStatus,largeOptions));for(int i=0;i<4000&&!frameStatus->Snapshot().eof;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(frameStatus->Snapshot().eof);CHECK(frameLimited.Seekable());CHECK(!frameLimited.Seek(1));CHECK(frameLimited.Error().find("index exceeds resource limit")!=std::string::npos);CHECK(!frameLimited.DurationKnown());frameLimited.Close();
  Decoder cancelled;CHECK(cancelled.Open(base+"/resume-unknown",&token,1,status));for(int i=0;i<1000&&!status->Snapshot().eof;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));CHECK(status->Snapshot().eof);token=2;CHECK(!cancelled.Seek(1));CHECK(!cancelled.Error().empty());cancelled.Close();
 }
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
 auto prefix=FetchPrefix(base+"/tone",16); CHECK(prefix.error.empty() && prefix.bytes.size()==16);
 auto ignoredPrefix=FetchPrefix(base+"/ignored",16);CHECK(ignoredPrefix.error.empty()&&ignoredPrefix.bytes==prefix.bytes);
 auto prefixTruncated=FetchPrefix(base+"/prefix-truncated",65536);CHECK(!prefixTruncated.error.empty()&&prefixTruncated.bytes.empty());
 auto prefixRedirect=FetchPrefix(base+"/redirect",16);CHECK(prefixRedirect.error.empty()&&prefixRedirect.bytes==prefix.bytes&&prefixRedirect.finalUrl==base+"/tone");
 auto prefixBad=FetchPrefix(base+"/bad-range",16);CHECK(!prefixBad.error.empty());
 auto prefixStart=std::chrono::steady_clock::now();auto timed=FetchPrefix(base+"/stalled",16,nullptr,0,150);CHECK(!timed.error.empty());CHECK(std::chrono::steady_clock::now()-prefixStart<std::chrono::seconds(2));
 std::atomic<uint64_t>prefixToken{1};FetchResult prefixCancelled;std::thread prefixWorker([&]{prefixCancelled=FetchPrefix(base+"/stalled",16,&prefixToken,1);});std::this_thread::sleep_for(std::chrono::milliseconds(100));prefixToken=2;prefixWorker.join();CHECK(prefixCancelled.cancelled&&prefixCancelled.bytes.empty());
 // Only the requested current cloud track triggers prefix enrichment and exact lyrics lookup.
 MediaCache cloudCache("build-host/stream/cloud-cache");cloudCache.Warm();cloudCache.ClearIdle();
 Track cloud;cloud.id="cloud:fixture:song";cloud.source=SourceKind::Cloud;cloud.path=base+"/metadata";cloud.title="Test song";
 auto waitArtwork=[](ArtworkLoader& loader){ArtworkSnapshot result;for(int i=0;i<2000;++i){result=loader.Snapshot();if(!result.loading)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}return result;};
 // Artwork must be usable while a slow fallback lyric request is still active.
 {MediaCache progressive("build-host/stream/progressive-cache");progressive.Warm();progressive.ClearIdle();
  ArtworkLoader loader(progressive,base+"/lyrics-slow");loader.Request(cloud);bool published=false;
  for(int i=0;i<500;++i){auto snapshot=loader.Snapshot();if(snapshot.cover&&snapshot.loading){published=true;break;}if(!snapshot.loading)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  CHECK(published);CHECK(waitArtwork(loader).lyrics);}
 // A complete catalog needs no extra audio prefix probe, even on refresh.
 {Track complete=cloud;complete.artist="Test artist";complete.album="Test album";complete.duration=123;
  complete.cover="build-host/assets-fixtures/cover.png";complete.lyrics="build-host/assets-fixtures/lyrics.lrc";
  auto before=FetchUrl(base+"/metadata-count",100);cloudCache.ResolveCloud(complete,nullptr,0,true);
  auto after=FetchUrl(base+"/metadata-count",100);CHECK(before.bytes==after.bytes);}
 {ArtworkLoader loader(cloudCache,base+"/lyrics");loader.Request(cloud);auto result=waitArtwork(loader);
 CHECK(!result.loading && result.enrichedTrack && result.enrichedTrack->artist=="Test artist" && result.enrichedTrack->album=="Test album" && result.enrichedTrack->duration==123);
 CHECK(result.cover && result.lyrics && result.lyrics->lines.size()==1 && result.trackPath==cloud.path);
 auto before=FetchUrl(base+"/metadata-count",100);loader.Request(cloud);auto cached=waitArtwork(loader);CHECK(cached.cover&&cached.lyrics);
 auto after=FetchUrl(base+"/metadata-count",100);CHECK(before.bytes==after.bytes);
 // Explicit references survive metadata fill; stale requests never publish an old identity.
 Track newer=cloud;newer.id="new";newer.path=base+"/metadata-uslt";newer.artist="Explicit artist";newer.cover="/missing-explicit-cover";
 loader.Request(cloud,true);loader.Request(newer);auto replaced=waitArtwork(loader);CHECK(replaced.trackId=="new"&&replaced.trackPath==newer.path&&replaced.enrichedTrack->artist=="Explicit artist"&&replaced.enrichedTrack->cover=="/missing-explicit-cover");CHECK(replaced.lyrics&&replaced.lyrics->lines.size()==1);
 }
 // A successful response for the wrong song is never displayed and is negatively cached.
 MediaCache wrongCache("build-host/stream/wrong-cache");wrongCache.Warm();wrongCache.ClearIdle();
 {ArtworkLoader loader(wrongCache,base+"/lyrics-wrong");loader.Request(cloud);auto wrong=waitArtwork(loader);CHECK(!wrong.lyrics);auto before=FetchUrl(base+"/lyrics-count",100);loader.Request(cloud);auto second=waitArtwork(loader);CHECK(!second.lyrics);auto after=FetchUrl(base+"/lyrics-count",100);CHECK(before.bytes==after.bytes);}
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
 CHECK(review(base,"cached-resume")==0);
 std::puts("PASS controlled HTTP startup, unknown duration, bounds, redirects, Range, truncation, 404, cancellation");
}
