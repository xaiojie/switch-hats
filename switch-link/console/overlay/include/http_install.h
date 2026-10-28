#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SlHttpInstall {
    char id[33];
    char name[201];
    char wire_name[234];
    char url[1025];
    char result_url[257];
    uint64_t size;
} SlHttpInstall;
bool sl_parse_http_install(const char*,size_t,const char*,const char*,SlHttpInstall*);
bool sl_http_install_can_accept(const char*,const char*,bool,bool);
bool sl_http_install_success(uint32_t,unsigned);
#ifdef __cplusplus
}
#endif
