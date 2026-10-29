#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sys/stat.h>

using namespace papa;
static int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); return 1; } } while(0)
int main(int argc, char** argv) {
    Queue q(52); q.Reset(3); CHECK(q.Current() == -1);
    CHECK(q.Next() == 0); CHECK(q.Next() == 1); CHECK(q.Previous() == 0);
    q.Select(2); CHECK(q.Next() == 0);
    q.SetMode(Mode::Sequential); q.Select(2); CHECK(q.Next(true) == -1); CHECK(q.Current() == 2);
    q.SetMode(Mode::RepeatOne); q.Select(1); CHECK(q.Next(true) == 1); CHECK(q.Next(false) == 2);
    q.Select(-2); CHECK(q.Current() == 2); q.Select(40); CHECK(q.Current() == 2);
    q.Reset(0); CHECK(q.Next() == -1); CHECK(q.Previous() == -1);
    q.Reset(1); q.SetMode(Mode::Shuffle); CHECK(q.Next() == 0); CHECK(q.Next() == 0);
    q.Reset(8,0); q.SetMode(Mode::Shuffle); std::set<int> seen{0};
    for (int i=0;i<7;++i) CHECK(seen.insert(q.Next()).second);
    int last=q.Current(); CHECK(q.Next()!=last); CHECK(q.Previous()==last);
    CHECK(TimeLabel(-12)=="00:00"); CHECK(TimeLabel(65.9)=="01:05"); CHECK(TimeLabel(3600)=="60:00");
    CHECK(TimeLabel(NAN)=="00:00");
    for (int rate : {22050,44100,48000,96000}) for(int ch : {1,2}) {
        Resampler r; r.Reset(rate,ch); size_t total=0; std::vector<int16_t> decoded;
        std::vector<int16_t> input(static_cast<size_t>(rate)*ch);
        for (int n=0;n<rate;++n) for (int c=0;c<ch;++c) input[n*ch+c]=static_cast<int16_t>(9000*std::sin(n*0.01));
        int16_t output[2*173];
        for (int start=0;start<rate;start+=317) {
            int n=std::min(317,rate-start); r.Push(input.data()+start*ch,n);
            size_t got; while ((got=r.Pull(output,173))>0) { total+=got; decoded.insert(decoded.end(),output,output+got*2); }
        }
        size_t got; while ((got=r.Pull(output,173,true))>0) { total+=got; decoded.insert(decoded.end(),output,output+got*2); }
        CHECK(total==48000); CHECK(decoded.size()==96000);
        for(size_t n=0;n<decoded.size();n+=2) { if(decoded[n]!=decoded[n+1]) { CHECK(false); } }
        for(size_t n=2;n<decoded.size();n+=2) if(std::abs(static_cast<int>(decoded[n])-decoded[n-2])>500) { CHECK(false); }
    }
    Resampler one; one.Reset(48000,1); int16_t sample=1234, out[8]; one.Push(&sample,1);
    CHECK(one.Pull(out,4)==0); CHECK(one.Pull(out,4,true)==1); CHECK(out[0]==1234&&out[1]==1234);
    CHECK(one.Pull(out,4,true)==0);
    if(argc>1) {
        auto tracks=ScanLibrary(argv[1]); CHECK(tracks.size()==3);
        CHECK(tracks[0].title=="Alpha"); CHECK(tracks[0].artist=="Singer");
        CHECK(ScanLibrary(argv[1],1).size()==1); CHECK(ScanLibrary(argv[1],100,0).size()==2);
        std::string conf=std::string(argv[1])+"/settings.ini";
        CHECK(SaveSettings(conf,Settings{200,Mode::Shuffle}));
        auto s=LoadSettings(conf); CHECK(s.volume==100); CHECK(s.mode==Mode::Shuffle);
        std::ofstream(conf)<<"volume=-90\nmode=999\n";
        s=LoadSettings(conf); CHECK(s.volume==0); CHECK(s.mode==Mode::LoopAll);
        CHECK(LoadSettings(conf+".missing").volume==70);
        CHECK(SaveSettings(conf,Settings{45,Mode::RepeatOne}));
        CHECK(LoadSettings(conf).volume==45);
        std::remove((conf+".bak").c_str());
        CHECK(std::rename(conf.c_str(),(conf+".bak").c_str())==0);
        CHECK(LoadSettings(conf).volume==45);
        CHECK(LoadSettings(conf).mode==Mode::RepeatOne);
        CHECK(SaveSettings(conf,Settings{65,Mode::Sequential}));
        CHECK(LoadSettings(conf).volume==65);
    }
    std::printf("PASS %d checks: queue, shuffle, streaming resampling, library, settings\n",checks);
}
