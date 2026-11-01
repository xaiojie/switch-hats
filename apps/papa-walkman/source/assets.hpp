#pragma once
#include "media.hpp"
#include "stream.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
namespace papa {
struct CoverImage {int width=0,height=0;std::vector<uint32_t> pixels;};
std::shared_ptr<CoverImage> DecodeCover(const std::vector<uint8_t>& bytes,int maximumEdge=384);
bool CreateDirectories(const std::string& path);
struct CacheUsage {uint64_t bytes=0,files=0;bool ready=false;};
class MediaCache {
public:
    explicit MediaCache(std::string root,uint64_t limit=512ull*1024*1024);
    const std::string& Root()const{return root_;}
    std::string MetadataDirectory()const{return root_+"/metadata";}
    std::string StreamDirectory()const{return root_+"/streams";}
    // Warm is idempotent and performs cold enumeration on the caller's background thread.
    // Usage is a constant-time snapshot, including during Warm. Writes require ready.
    void Warm();
    void RecordAsset(const std::string& path); // also records replacement or removal; managed cache children only
    void RefreshStreams(bool force=true); // scans only the small stream directory, never evicts active streams
    void Enrich(Track& track); // serialized, budgeted derived-art generation and index recording
    CacheUsage Usage()const;
    uint64_t Limit()const{return limit_.load();}
    void SetLimit(uint64_t value);
    void SetPins(const std::vector<std::string>& paths);
    std::string AssetPath(const std::string& reference) const;
    bool Reserve(uint64_t bytes,const std::vector<std::string>& pinned={});
    bool Prune(const std::vector<std::string>& pinned={});
    void ClearIdle(const std::vector<std::string>& pinned={});
    void RemoveStartupOrphans();
    std::vector<uint8_t> ReadAsset(const std::string& reference,size_t maximum,
        const std::atomic<uint64_t>*cancel=nullptr,uint64_t expected=0,bool refresh=false);
    // Atomic bounded writes into managed metadata/assets directories; never holds
    // the index lock while writing a file. Serializes reservations and promotion.
    bool Store(const std::string& path,const std::vector<uint8_t>& bytes,const std::vector<std::string>& pinned={},
        const std::atomic<uint64_t>*cancel=nullptr,uint64_t expected=0);
    void ResolveCloud(Track& track,const std::atomic<uint64_t>*cancel,uint64_t expected,bool refresh=false);
    void ResolveLyrics(Track& track,const std::string& service,const std::atomic<uint64_t>*cancel,uint64_t expected,bool refresh=false);
private:
    struct Indexed {uint64_t bytes=0,modified=0;bool stream=false;};
    bool ManagedPath(const std::string& path)const;
    void RecordLocked(const std::string& path);
    void PublishLocked();
    bool TrimLocked(uint64_t target,const std::set<std::string>&keep);
    std::string root_;std::atomic<uint64_t> limit_;mutable std::mutex mutex_,usageMutex_;
    std::mutex warmMutex_,writeMutex_,streamMutex_;
    std::chrono::steady_clock::time_point streamRefresh_{};
    std::set<std::string>pins_,changed_,streams_;
    std::map<std::string,Indexed>index_;
    uint64_t total_=0;bool ready_=false;
    CacheUsage usage_;
};
struct ArtworkSnapshot {
    std::string trackId,trackPath,note;
    std::shared_ptr<const Track> enrichedTrack;
    bool loading=false;
    std::shared_ptr<CoverImage> cover;
    std::shared_ptr<Lyrics> lyrics;
};
class ArtworkLoader {
public:
    explicit ArtworkLoader(MediaCache& cache,std::string lyricsService="https://lrclib.net");
    ~ArtworkLoader();
    void Request(const Track& track,bool refresh=false);
    ArtworkSnapshot Snapshot();
private:
    void Run();MediaCache&cache_;std::string lyricsService_;std::mutex mutex_;std::condition_variable changed_;
    std::atomic<uint64_t>generation_{0};bool running_=true,pending_=false,refresh_=false;
    Track requested_;ArtworkSnapshot snapshot_;std::thread thread_;
};
class ThumbnailLoader {
public:
    explicit ThumbnailLoader(MediaCache& cache);
    ~ThumbnailLoader();
    std::shared_ptr<CoverImage> Get(const Track& track);
    void Clear(bool refresh=false);
private:
    void Run();MediaCache&cache_;std::mutex mutex_;std::condition_variable changed_;
    std::atomic<uint64_t>generation_{0};bool running_=true,refresh_=false;uint64_t serial_=0;
    std::deque<Track>queued_;std::set<std::string>pending_;
    std::map<std::string,std::pair<std::shared_ptr<CoverImage>,uint64_t>>ready_;
    std::thread thread_;
};
}
