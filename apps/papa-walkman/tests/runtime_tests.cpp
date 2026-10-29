#include "runtime.hpp"
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
using namespace papa;
#define CHECK(x) do{if(!(x)){std::printf("FAIL runtime %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
 std::string root="build-host/runtime-fixture";CHECK(CreateDirectories(root+"/music"));
 auto path=root+"/music/tone.mp3",index=root+"/index.json";
 {std::ofstream f(path,std::ios::binary);f<<"synthetic audio fixture";}
 struct stat st{};CHECK(!stat(path.c_str(),&st));
 Track track{path,"cached base title","cached artist"};track.id="sd:cached";track.fileSize=st.st_size;track.modified=st.st_mtime;
 CHECK(SaveLibraryIndex(index,{track}));MediaCache cache(root+"/cache");
 std::map<std::string,MediaOverride>overrides;overrides[track.id].title="user override";std::atomic<uint64_t>cancel{1};std::string error;
 auto result=ScanWithMetadata(root+"/music",index,cache,overrides,cancel,1,error);
 CHECK(error.empty());CHECK(result.size()==1);CHECK(result[0].title=="user override");CHECK(result[0].artist=="cached artist");
 std::vector<Track>raw;CHECK(LoadLibraryIndex(index,raw));CHECK(raw.size()==1);CHECK(raw[0].title=="cached base title");
 {std::ofstream f(path,std::ios::binary|std::ios::app);f<<" size changed";}
 result=ScanWithMetadata(root+"/music",index,cache,{},cancel,1,error);
 CHECK(result.size()==1);CHECK(result[0].title!="cached base title");CHECK(result[0].fileSize>track.fileSize);
 cancel=2;CHECK(ScanWithMetadata(root+"/music",index,cache,{},cancel,1,error).empty());
 MediaSource a{"same","source","https://a.invalid/list.json"},b=a;b.url="https://b.invalid/list.json";
 CHECK(CatalogCachePath(cache,a)!=CatalogCachePath(cache,b));
 std::remove(path.c_str());std::remove(index.c_str());std::remove((index+".bak").c_str());cache.ClearIdle();
 std::puts("PASS runtime: metadata tuple reuse, raw index before overrides, invalidation, cancellation, source URL cache isolation");
}
