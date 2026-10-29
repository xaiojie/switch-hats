#include "http_install.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(expr) do { if (!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(1); } } while(0)
#include <string.h>
static const char* device="0123456789abcdef0123456789abcdef";
static const char* good="{\"type\":\"switch-link-install\",\"version\":1,\"device_id\":\"0123456789abcdef0123456789abcdef\",\"id\":\"1123456789abcdef0123456789abcdef\",\"name\":\"中文 A.nsp\",\"wire_name\":\"1123456789abcdef0123456789abcdef_中文 A.nsp\",\"url\":\"http://192.168.1.2:8080/link-files/1123456789abcdef0123456789abcdef/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/%E4%B8%AD%E6%96%87%20A.nsp\",\"result_url\":\"http://192.168.1.2:8080/link-result/1123456789abcdef0123456789abcdef/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"size\":3005823376}";
static void replace(char* out,const char* needle,const char* replacement) {
    const char* at=strstr(good,needle); assert(at); size_t n=at-good;
    memcpy(out,good,n); strcpy(out+n,replacement); strcat(out,at+strlen(needle));
}
static void make_command(char* json,const char* name,const char* encoded) {
    snprintf(json,8192,"{\"type\":\"switch-link-install\",\"version\":1,\"device_id\":\"0123456789abcdef0123456789abcdef\",\"id\":\"1123456789abcdef0123456789abcdef\",\"name\":\"%s\",\"wire_name\":\"1123456789abcdef0123456789abcdef_%s\",\"url\":\"http://192.168.1.2:8080/link-files/1123456789abcdef0123456789abcdef/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/%s\",\"result_url\":\"http://192.168.1.2:8080/link-result/1123456789abcdef0123456789abcdef/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"size\":1}",name,name,encoded);
}
int main(void) {
    SlHttpInstall cmd={0}; char json[8192];
    assert(sl_parse_http_install(good,strlen(good),device,"192.168.1.2",&cmd));
    assert(cmd.size==3005823376ULL && !strcmp(cmd.name,"中文 A.nsp"));
    assert(!sl_parse_http_install(good,strlen(good),device,"192.168.1.3",&cmd));
    assert(!sl_parse_http_install(good,strlen(good),"ffffffffffffffffffffffffffffffff","192.168.1.2",&cmd));
    const char* needles[]={"\"version\":1","\"size\":3005823376","\"size\":3005823376","\"size\":3005823376","\"size\":3005823376","\"size\":3005823376","\"name\":\"中文 A.nsp\"","\"name\":\"中文 A.nsp\"","\"name\":\"中文 A.nsp\"","\"type\":\"switch-link-install\"","\"wire_name\":\"1123456789abcdef0123456789abcdef_中文 A.nsp\"","http://192.168.1.2:8080/link-files","http://192.168.1.2:8080/link-files","http://192.168.1.2:8080/link-result","/link-result/1123456789abcdef0123456789abcdef/a","%E4%B8%AD%E6%96%87%20A.nsp","\"id\":\"1123456789abcdef0123456789abcdef\""};
    const char* bad[]={"\"version\":2","\"size\":0","\"size\":-1","\"size\":137438953473","\"size\":18446744073709551616","\"size\":3005823376.0","\"name\":\"../x.nsp\"","\"name\":\"x\\u0000.nsp\"","\"name\":\"x\\n.nsp\"","\"type\":\"switch-link-task\"","\"wire_name\":\"bad.nsp\"","https://192.168.1.2:8080/link-files","http://192.168.1.2@evil.com:8080/link-files","http://192.168.1.2:8081/link-result","/link-result/2123456789abcdef0123456789abcdef/a","%E4%B8%AD%E6%96%87%20A.nsp?x=y","\"id\":\"1123456789abcdef0123456789abcdeg\""};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        replace(json,needles[i],bad[i]);
        if(sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd)) { fprintf(stderr,"accepted malformed case %zu\n",i); return 1; }
    }
    replace(json,"3005823376","137438953472");
    assert(sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd) && cmd.size==137438953472ULL);
    assert(!sl_http_install_can_accept(cmd.id,cmd.id,false,true));
    assert(!sl_http_install_can_accept(cmd.id,"",true,true));
    assert(!sl_http_install_can_accept(cmd.id,"",false,false));
    assert(sl_http_install_can_accept(cmd.id,"",false,true));
    assert(sl_http_install_success(0,1) && !sl_http_install_success(0,0) && !sl_http_install_success(1,1));
    // A restarted receiver reloads the persisted ID: the same command cannot run again.
    assert(!sl_http_install_can_accept(cmd.id,"1123456789abcdef0123456789abcdef",false,true));
    assert(!sl_parse_http_install(good,4097,device,"192.168.1.2",&cmd));
    assert(!sl_parse_http_install(NULL,0,device,"192.168.1.2",&cmd));
    assert(!sl_parse_http_install("{}",2,device,"192.168.1.2",&cmd));
    replace(json,"\"version\":1", "\"version\":1,\"version\":1");
    assert(!sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    make_command(json,"game.nsz","game.nsz");
    assert(!sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    make_command(json,"game.xcz","game.xcz");
    assert(!sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    make_command(json,"game.XCI","game.XCI");
    assert(sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    char name[205]; memset(name,'a',196); strcpy(name+196,".nsp");
    make_command(json,name,name);
    assert(sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    memset(name,'a',197); strcpy(name+197,".nsp");
    make_command(json,name,name);
    assert(!sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    // 65 Chinese characters (195 UTF-8 bytes) plus suffix fit; 66 do not.
    char encoded[610]; name[0]=encoded[0]=0;
    for(unsigned i=0;i<65;i++) { strcat(name,"中");strcat(encoded,"%E4%B8%AD"); }
    strcat(name,".nsp");strcat(encoded,".nsp");make_command(json,name,encoded);
    assert(sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    name[195]=0;strcat(name,"中.nsp");make_command(json,name,encoded);
    assert(!sl_parse_http_install(json,strlen(json),device,"192.168.1.2",&cmd));
    // Output is untouched by a rejected packet.
    strcpy(cmd.name,"keep");
    assert(!sl_parse_http_install("{",1,device,"192.168.1.2",&cmd) && !strcmp(cmd.name,"keep"));
    for(unsigned seed=1;seed<1000;seed++) {
        unsigned v=seed; for(unsigned i=0;i<64;i++) {v=v*1664525U+1013904223U;json[i]=v>>24;}
        assert(!sl_parse_http_install(json,64,device,"192.168.1.2",&cmd));
    }
    puts("HTTP command identity, origin, capability, 64-bit bounds, malformed, readiness and replay tests passed.");
}
