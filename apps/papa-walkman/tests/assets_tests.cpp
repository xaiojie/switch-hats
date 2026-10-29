#include "assets.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
using namespace papa;
#define CHECK(x)do{if(!(x)){std::fprintf(stderr,"FAIL assets line %d: %s\n",__LINE__,#x);return 1;}}while(0)
void append(void*context,void*data,int size){auto*v=static_cast<std::vector<uint8_t>*>(context);auto*p=static_cast<uint8_t*>(data);v->insert(v->end(),p,p+size);}
int main(){
    std::vector<uint8_t>raw(16*8*4,255),png;
    CHECK(stbi_write_png_to_func(append,&png,16,8,4,raw.data(),16*4));
    auto image=DecodeCover(png,8);CHECK(image&&image->width==8&&image->height==4);CHECK(image->pixels.size()==32);
    CHECK(!DecodeCover({1,2,3,4},8));
    CHECK(CreateDirectories("build-host/assets-fixtures/cache/metadata"));
    const char*file="build-host/assets-fixtures/cover.png";
    FILE*f=OpenUtf8File(file,"wb");CHECK(f);CHECK(std::fwrite(png.data(),1,png.size(),f)==png.size());std::fclose(f);
    MediaCache cold("build-host/assets-fixtures/cold"); CHECK(!cold.Reserve(1));
    MediaCache cache("build-host/assets-fixtures/cache",1024*1024);
    cache.Warm();
    CHECK(cache.ReadAsset(file,4096)==png);CHECK(cache.ReadAsset(file,4).empty());
    std::string lyric="build-host/assets-fixtures/lyrics.lrc";
    f=OpenUtf8File(lyric,"wb");CHECK(f);std::fputs("[offset:500]\n[00:01.00]Original test line\n[00:02.00]Second test line\n",f);std::fclose(f);
    ArtworkLoader loader(cache);Track a;a.id="a";a.cover=file;a.lyrics=lyric;
    loader.Request(a);Track b=a;b.id="b";loader.Request(b);
    ArtworkSnapshot result;
    for(int i=0;i<200;++i){result=loader.Snapshot();if(result.trackId=="b"&&!result.loading)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(result.trackId=="b"&&!result.loading);CHECK(result.cover&&result.lyrics);CHECK(result.lyrics->Current(.6)==0);
    std::string kept=cache.MetadataDirectory()+"/aaaaaaaaaaaaaaaa.jpg",old=cache.MetadataDirectory()+"/bbbbbbbbbbbbbbbb.jpg";
    for(const auto&p:{kept,old}){f=OpenUtf8File(p,"wb");CHECK(f);for(int i=0;i<4096;++i)std::fputc(1,f);std::fclose(f);cache.RecordAsset(p);}
    cache.SetLimit(5000);CHECK(cache.Prune({kept}));CHECK(cache.Usage().bytes<=5000);
    f=OpenUtf8File(kept,"rb");CHECK(f);std::fclose(f);CHECK(!OpenUtf8File(old,"rb"));
    cache.ClearIdle({kept});CHECK(cache.Usage().files==1);
    const std::string catalog=cache.MetadataDirectory()+"/cccccccccccccccc.catalog.json";
    f=OpenUtf8File(catalog,"wb");CHECK(f);std::fputs("catalog fixture",f);std::fclose(f);cache.RecordAsset(catalog);
    cache.SetPins({catalog});cache.ClearIdle();f=OpenUtf8File(catalog,"rb");CHECK(f);std::fclose(f);
    cache.SetPins({});cache.ClearIdle();CHECK(cache.Usage().files==0);
    CHECK(cache.Reserve(4000));
    CHECK(cache.AssetPath("http://example.test/cover.jpg").find("papa-asset-")!=std::string::npos);
    ThumbnailLoader thumbs(cache);std::shared_ptr<CoverImage>small;
    for(int i=0;i<200;++i){small=thumbs.Get(b);if(small)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(small&&small->width==16&&small->height==8);thumbs.Clear();
    // Regenerating an evicted embedded lyric must clear its missing reference and record its new file.
    MediaCache derived("build-host/assets-fixtures/derived");derived.Warm();derived.ClearIdle();
    std::string payload=std::string("\x03" "eng\0",5)+"[00:01.00]Recovered lyric\n";
    std::string body="USLT";for(int i=3;i>=0;--i)body+=char((payload.size()>>(8*i))&255);body+=std::string(2,0)+payload;
    std::string tag="ID3";tag+=char(3);tag+=std::string(2,0);for(int i=3;i>=0;--i)tag+=char((body.size()>>(7*i))&127);tag+=body;
    std::string song="build-host/assets-fixtures/embedded.mp3";f=OpenUtf8File(song,"wb");CHECK(f);CHECK(std::fwrite(tag.data(),1,tag.size(),f)==tag.size());std::fclose(f);
    Track embedded;embedded.path=song;derived.Enrich(embedded);CHECK(!embedded.lyrics.empty()&&derived.Usage().files==1);
    CHECK(!std::remove(embedded.lyrics.c_str()));derived.RecordAsset(embedded.lyrics);CHECK(derived.Usage().files==0);
    {ArtworkLoader recovery(derived);recovery.Request(embedded);ArtworkSnapshot recovered;
        for(int i=0;i<200;++i){recovered=recovery.Snapshot();if(!recovered.loading)break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        CHECK(recovered.lyrics&&recovered.lyrics->lines.size()==1);CHECK(derived.Usage().files==1);}
    derived.ClearIdle();std::remove(song.c_str());
    // A fresh stream created by ByteSource must reduce the next reservation, never be evicted.
    MediaCache accounting("build-host/assets-fixtures/accounting",5000);accounting.Warm();accounting.RemoveStartupOrphans();accounting.ClearIdle();
    auto writeBytes=[](const std::string&p,size_t n){FILE*f=OpenUtf8File(p,"wb");if(!f)return false;std::vector<uint8_t>b(n,1);bool ok=std::fwrite(b.data(),1,b.size(),f)==b.size();return std::fclose(f)==0&&ok;};
    auto pinned=accounting.MetadataDirectory()+"/dddddddddddddddd.jpg";
    auto stream=accounting.StreamDirectory()+"/papa-stream-test.tmp";
    CHECK(writeBytes(pinned,4096));accounting.RecordAsset(pinned);accounting.SetPins({pinned});
    CHECK(writeBytes(stream,2048));CHECK(!accounting.Reserve(0));
    CHECK(accounting.Usage().bytes==6144&&accounting.Usage().files==2);
    accounting.ClearIdle();CHECK(accounting.Usage().bytes==6144);
    accounting.RemoveStartupOrphans();CHECK(accounting.Usage().bytes==4096);
    CHECK(writeBytes(pinned,100));accounting.RecordAsset(pinned);CHECK(accounting.Usage().bytes==100);
    MediaCache restarted(accounting.Root(),5000);CHECK(!restarted.Usage().ready);restarted.Warm();
    CHECK(restarted.Usage().ready&&restarted.Usage().bytes==100&&restarted.Usage().files==1);
    CHECK(!std::remove(pinned.c_str()));accounting.RecordAsset(pinned);CHECK(accounting.Usage().bytes==0&&accounting.Usage().files==0);
    auto empty=accounting.MetadataDirectory()+"/ffffffffffffffff.jpg";CHECK(writeBytes(empty,0));accounting.RecordAsset(empty);
    accounting.ClearIdle();CHECK(accounting.Usage().files==0);
    const std::string cachedUrl="http://127.0.0.1:1/offline-cover";auto urlPath=accounting.AssetPath(cachedUrl);
    CHECK(writeBytes(urlPath,4));accounting.RecordAsset(urlPath);accounting.SetPins({cachedUrl});accounting.ClearIdle();
    CHECK(accounting.ReadAsset(cachedUrl,8)==std::vector<uint8_t>(4,1));
    CHECK(accounting.ReadAsset(cachedUrl,8,nullptr,0,true)==std::vector<uint8_t>(4,1));
    accounting.SetPins({});accounting.ClearIdle();CHECK(accounting.Usage().files==0);
    // Unrelated private files and original audio outside the managed directories are never owned.
    auto original=accounting.Root()+"/original.mp3";CHECK(writeBytes(original,17));accounting.RecordAsset(original);
    CHECK(accounting.Usage().files==0);accounting.ClearIdle();CHECK(accounting.ReadAsset(original,100).size()==17);std::remove(original.c_str());
    // Real 10k-file cold discovery, snapshot access while it runs, and fit-budget operations.
    MediaCache big("build-host/assets-fixtures/big");big.Warm();big.ClearIdle();
    for(int i=0;i<10000;++i){char name[40];std::snprintf(name,sizeof(name),"/%016x.jpg",i);CHECK(writeBytes(big.MetadataDirectory()+name,1));}
    MediaCache background(big.Root());std::atomic<bool>done{false};
    auto begin=std::chrono::steady_clock::now();std::thread worker([&]{background.Warm();done=true;});
    // Concurrent writes and tombstones must win over the cold enumeration snapshot.
    auto concurrent=background.MetadataDirectory()+"/eeeeeeeeeeeeeeee.jpg";
    bool concurrentWritten=writeBytes(concurrent,7);background.RecordAsset(concurrent);
    auto removed=background.MetadataDirectory()+"/0000000000000000.jpg";
    bool concurrentRemoved=std::remove(removed.c_str())==0;background.RecordAsset(removed);
    uint64_t snapshots=0;while(!done){auto u=background.Usage();(void)u;++snapshots;std::this_thread::yield();}worker.join();
    CHECK(concurrentWritten&&concurrentRemoved);
    CHECK(background.Usage().ready&&background.Usage().files==10000&&background.Usage().bytes==10006);
    auto warmed=std::chrono::steady_clock::now();
    for(int i=0;i<10000;++i){CHECK(background.Prune());CHECK(background.Reserve(1));CHECK(background.Usage().files==10000);}
    auto finished=std::chrono::steady_clock::now();
    std::printf("BENCH cache 10000 files: warm=%lldms, 10000 prune/reserve/usage=%lldms, concurrent snapshots=%llu\n",static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(warmed-begin).count()),static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(finished-warmed).count()),static_cast<unsigned long long>(snapshots));
    background.ClearIdle();CHECK(background.Usage().files==0);
    std::puts("PASS artwork decoding/bounds, async generation, synced lyrics, pinned cache quota/cleanup");
}
