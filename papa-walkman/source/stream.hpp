#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace papa {
// seekable describes cached byte offsets, not MP3 time seeking (see Decoder::Seekable).
struct StreamSnapshot {
 bool buffering=false,eof=false,cancelled=false,rangeSupported=false,seekable=false;
 uint64_t downloadedBytes=0,totalBytes=0,cachedStart=0,cachedEnd=0;
 std::string finalUrl,error;
};
class StreamStatus {
public:
 StreamSnapshot Snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return value_;}
 void Update(const StreamSnapshot& value){std::lock_guard<std::mutex> lock(mutex_);value_=value;}
private: mutable std::mutex mutex_;StreamSnapshot value_;
};
struct StreamOptions {
 uint64_t cacheBytes=512ull*1024*1024;
 size_t prefetchBytes=256*1024;
 // Empty uses the platform temporary directory. Cache files are request-private and deleted on close.
 std::string cacheDirectory;
};
class ByteSource {
public:
 ByteSource();~ByteSource();
 bool Open(const std::string&,const std::atomic<uint64_t>* cancel=nullptr,uint64_t expected=0,
           std::shared_ptr<StreamStatus> status={},StreamOptions options={});
 // Single consumer. Read blocks through temporary starvation; zero means terminal status.
 // Close detaches cancellation promptly; the request owns its cache until cleanup finishes.
 void Close();size_t Read(void*,size_t);bool Seek(uint64_t);
 StreamSnapshot Status() const;bool IsRemote() const;
private:struct Impl;std::unique_ptr<Impl> p_;
};
struct FetchResult {std::vector<uint8_t> bytes;std::string finalUrl,error;bool cancelled=false;};
FetchResult FetchUrl(const std::string&,size_t maximumBytes,const std::atomic<uint64_t>* cancel=nullptr,uint64_t expected=0);
// UTF-8 paths on Windows, normal libc paths on Switch.
FILE* OpenUtf8File(const std::string&,const char* mode);
}
