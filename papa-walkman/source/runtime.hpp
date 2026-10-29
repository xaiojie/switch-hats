#pragma once
#include "assets.hpp"
namespace papa {
std::string CatalogCachePath(const MediaCache&cache,const MediaSource&source);
// Reuse base metadata only for an unchanged path,size,mtime tuple. Overrides
// deliberately run afterwards and never enter the reusable raw metadata index.
std::vector<Track> ScanWithMetadata(const std::string&root,const std::string&index,
 MediaCache&cache,const std::map<std::string,MediaOverride>&overrides,
 const std::atomic<uint64_t>&cancel,uint64_t generation,std::string&error,
 const std::vector<std::string>&pinned={});
struct LibraryLoadResult {std::vector<Track>sd,cloud;std::string notice;bool stale=false;};
LibraryLoadResult LoadLibraries(const std::string&root,const std::string&config,
 MediaCache&cache,const std::vector<MediaSource>&sources,
 const std::atomic<uint64_t>&cancel,uint64_t generation,const std::vector<std::string>&pinned={});
}
