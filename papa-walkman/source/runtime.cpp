#include "runtime.hpp"
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
namespace papa {
std::string CatalogCachePath(const MediaCache&c,const MediaSource&s){uint64_t h=14695981039346656037ull;for(unsigned char v:s.id+"\n"+s.url){h^=v;h*=1099511628211ull;}char key[40];std::snprintf(key,sizeof(key),"/%016llx.catalog.json",static_cast<unsigned long long>(h));return c.MetadataDirectory()+key;}
std::vector<Track>ScanWithMetadata(const std::string&root,const std::string&index,MediaCache&cache,
 const std::map<std::string,MediaOverride>&overrides,const std::atomic<uint64_t>&cancel,uint64_t generation,std::string&error,const std::vector<std::string>&pinned){
 std::vector<Track>old;LoadLibraryIndex(index,old);std::map<std::string,const Track*>byPath;for(const auto&t:old)byPath[t.path]=&t;
 auto tracks=ScanLibrary(root);std::vector<Track>raw;raw.reserve(tracks.size());
 for(auto&t:tracks){if(cancel.load()!=generation)return {};struct stat st{};if(stat(t.path.c_str(),&st))continue;
  uint64_t size=static_cast<uint64_t>(st.st_size),mtime=static_cast<uint64_t>(st.st_mtime);auto found=byPath.find(t.path);
  if(found!=byPath.end()&&found->second->fileSize==size&&found->second->modified==mtime)t=*found->second;
  else EnrichTrack(t,cache.MetadataDirectory());
  t.fileSize=size;t.modified=mtime;raw.push_back(t);cache.Prune(pinned);
 }
 if(cancel.load()!=generation)return {};
 SaveLibraryIndex(index,raw,&error);
 for(auto&t:raw){auto o=overrides.find(t.id);if(o!=overrides.end())ApplyOverride(t,o->second);}return raw;
}
LibraryLoadResult LoadLibraries(const std::string&root,const std::string&config,MediaCache&cache,const std::vector<MediaSource>&sources,
 const std::atomic<uint64_t>&cancel,uint64_t generation,const std::vector<std::string>&pinned){
 LibraryLoadResult out;std::map<std::string,MediaOverride>overrides;std::string error;LoadOverrides(config+"/overrides.json",overrides,&error);
 auto pins=pinned;for(const auto&s:sources)pins.push_back(CatalogCachePath(cache,s));
 cache.SetPins(pins);
 out.sd=ScanWithMetadata(root,config+"/sd-library.json",cache,overrides,cancel,generation,out.notice,pins);
 for(const auto&s:sources){if(!s.enabled)continue;if(cancel.load()!=generation)return out;
  auto result=FetchUrl(s.url,8*1024*1024,&cancel,generation);std::vector<Track>tracks;
  error=result.error;if(error.empty()&&!result.cancelled)tracks=ParseCatalog(std::string(result.bytes.begin(),result.bytes.end()),result.finalUrl,s.id,error);
  if(result.cancelled||cancel.load()!=generation)return out;
  auto path=CatalogCachePath(cache,s);
  if(!error.empty()){out.stale=true;out.notice=s.name+": "+error+"（保留上次曲库）";tracks.clear();LoadLibraryIndex(path,tracks);}
  else {std::string saveError;
   if(!cache.Reserve(16ull*1024*1024,pins))out.notice="已更新但离线清单未保存，缓存空间不足";
   else if(!SaveLibraryIndex(path,tracks,&saveError))out.notice=saveError;
  }
  for(auto&t:tracks){if(out.sd.size()+out.cloud.size()>=10000){out.notice="曲库已达 10000 首上限";break;}
   auto o=overrides.find(t.id);if(o!=overrides.end())ApplyOverride(t,o->second);out.cloud.push_back(std::move(t));}
  cache.Prune(pins);
 }
 return out;
}
}
