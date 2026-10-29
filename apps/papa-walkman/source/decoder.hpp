#pragma once
#include <atomic>
#include "stream.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
namespace papa {
class Decoder {
public:
    Decoder(); ~Decoder();
    Decoder(const Decoder&)=delete; Decoder& operator=(const Decoder&)=delete;
    bool Open(const std::string& path, const std::atomic<uint64_t>* cancel=nullptr, uint64_t expected=0, std::shared_ptr<StreamStatus> status={}, StreamOptions options={});
    void Close();
    bool Seek(double seconds);
    size_t Read(int16_t* samples, size_t frames);
    int Rate() const; int Channels() const;
    double Duration() const;
    bool DurationKnown() const; bool Seekable() const; bool Buffering() const;
    std::string Error() const;
private:
    struct Impl; std::unique_ptr<Impl> p_;
};
}
