#include "decoder.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace papa;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL decoder line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
    CHECK(argc==2); Decoder d; CHECK(!d.Open("missing.mp3")); CHECK(!d.Error().empty());
    CHECK(d.Open(argv[1])); CHECK(d.Rate()==44100); CHECK(d.Channels()==2);
    CHECK(d.Duration()>1.9&&d.Duration()<2.2);
    std::vector<int16_t> block(1024*2); size_t frames=0; long long energy=0;
    size_t n; while((n=d.Read(block.data(),1024))){frames+=n;for(size_t i=0;i<n*2;++i)energy+=std::abs(static_cast<int>(block[i]));}
    CHECK(frames>85000&&frames<97000); CHECK(energy>10000000); CHECK(d.Error().empty());
    CHECK(d.Seek(0.8)); CHECK(d.Read(block.data(),1024)==1024);
    CHECK(d.Seek(-50)); CHECK(d.Read(block.data(),1024)==1024);
    std::atomic<uint64_t> generation{9}; CHECK(!d.Open(argv[1],&generation,8));
    CHECK(!d.Open("build-host/fixtures/Singer - Alpha.MP3"));
    std::printf("PASS %d checks: real MP3 stream, duration, seek, cancellation, invalid files\n",checks);
}
