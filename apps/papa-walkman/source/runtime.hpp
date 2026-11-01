#pragma once
#include "assets.hpp"
namespace papa {
struct MaintenanceSnapshot {
 bool running=false,busy=false;
 uint64_t queued=0,completed=0;
 std::string error;
};
// Owns its worker through Shutdown/destruction. Queue/status calls never perform
// disk work; Flush is deliberately blocking and belongs only on the exit path.
class MaintenanceWorker {
public:
 MaintenanceWorker(MediaCache&cache,std::string configRoot,std::string settingsPath);
 ~MaintenanceWorker();
 MaintenanceWorker(const MaintenanceWorker&)=delete;
 MaintenanceWorker&operator=(const MaintenanceWorker&)=delete;
 // full=false copies only resume. A newer resume/settings request never erases
 // an outstanding full save (favorites, playlists, sources, lyric offsets).
 void QueueSave(Settings settings,const MediaState&state,bool full=true);
 void QueueSources(std::vector<MediaSource>sources);
 void QueueCacheMaintenance(std::vector<std::string>pins,bool clear=false,bool persistLimit=false);
 MaintenanceSnapshot Snapshot()const;
 bool Flush();
 void Shutdown();
private:
 struct Impl;
 std::unique_ptr<Impl>impl_;
};
std::string CatalogCachePath(const MediaCache&cache,const MediaSource&source);
std::string PreferredCatalogUrl(const std::string&url);
// Reuse base metadata only for an unchanged path,size,mtime tuple. Overrides
// deliberately run afterwards and never enter the reusable raw metadata index.
std::vector<Track> ScanWithMetadata(const std::string&root,const std::string&index,
 MediaCache&cache,const std::map<std::string,MediaOverride>&overrides,
 const std::atomic<uint64_t>&cancel,uint64_t generation,std::string&error,
 const std::vector<std::string>&pinned={});
struct LibraryLoadResult {std::vector<Track>sd,cloud;std::vector<MediaSource>sources;std::string notice;bool stale=false;};
LibraryLoadResult LoadLibraries(const std::string&root,const std::string&config,
 MediaCache&cache,const std::vector<MediaSource>&sources,
 const std::atomic<uint64_t>&cancel,uint64_t generation,const std::vector<std::string>&pinned={});
}
