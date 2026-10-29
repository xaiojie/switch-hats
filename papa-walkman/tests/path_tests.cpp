#include "core.hpp"
#include <cstdio>
#include <cwchar>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL UTF-8 path line %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main(){
#ifdef _WIN32
    CreateDirectoryW(L"build-host",nullptr);CreateDirectoryW(L"build-host/路径测试",nullptr);
    FILE*f=_wfopen(L"build-host/路径测试/歌手 - 测试.MP3",L"wb");
#else
    mkdir("build-host",0777);mkdir("build-host/路径测试",0777);
    FILE*f=std::fopen("build-host/路径测试/歌手 - 测试.MP3","wb");
#endif
    CHECK(f);std::fputs("scan-only fixture",f);std::fclose(f);
    auto tracks=papa::ScanLibrary("build-host/路径测试");
    CHECK(tracks.size()==1);CHECK(tracks[0].title=="测试");CHECK(tracks[0].artist=="歌手");
    CHECK(tracks[0].fileSize==17);CHECK(tracks[0].modified>0);
    const std::string path="build-host/路径测试/配置.ini";
    CHECK(papa::SaveSettings(path,papa::Settings{55,papa::Mode::Shuffle}));
    CHECK(papa::LoadSettings(path).volume==55);
    CHECK(papa::SaveSettings(path,papa::Settings{85,papa::Mode::RepeatOne}));
    auto settings=papa::LoadSettings(path);CHECK(settings.volume==85);CHECK(settings.mode==papa::Mode::RepeatOne);
    std::puts("PASS UTF-8 directory scan, titles, settings creation/replacement/readback");
}
