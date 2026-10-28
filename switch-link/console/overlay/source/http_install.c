#include "http_install.h"
#include <yyjson.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static bool hex(const char* s,size_t length) {
    if(strlen(s)!=length) return false;
    for(size_t i=0;i<length;i++) if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    return true;
}
static bool text(yyjson_val* obj,const char* key,char* dst,size_t cap) {
    yyjson_val* val=yyjson_obj_get(obj,key);
    if(!yyjson_is_str(val)) return false;
    size_t len=yyjson_get_len(val); const char* s=yyjson_get_str(val);
    if(!len||len>=cap||strlen(s)!=len) return false;
    for(size_t i=0;i<len;i++) if((unsigned char)s[i]<32||s[i]==127) return false;
    memcpy(dst,s,len+1); return true;
}
static bool filename(const char* name) {
    const size_t len=strlen(name);
    if(len<5||len>200||strpbrk(name,"/\\:")||strstr(name,"..")) return false;
    const char* ext=name+len-4;
    return !strcmp(ext,".nsp")||!strcmp(ext,".xci")||
        ((ext[0]=='.')&&tolower((unsigned char)ext[1])=='n'&&tolower((unsigned char)ext[2])=='s'&&tolower((unsigned char)ext[3])=='p')||
        ((ext[0]=='.')&&tolower((unsigned char)ext[1])=='x'&&tolower((unsigned char)ext[2])=='c'&&tolower((unsigned char)ext[3])=='i');
}
// No DNS, credentials, redirects, fragments or query strings. Both capability
// URLs must use the command sender's literal IPv4 and identical HTTP authority.
static const char* origin(const char* url,const char* sender,char* dest,size_t cap) {
    if(strncmp(url,"http://",7)) return NULL;
    const char* path=strchr(url+7,'/');
    if(!path) return NULL;
    size_t n=path-(url+7), hostlen=strlen(sender);
    if(n>=cap||n<hostlen||strncmp(url+7,sender,hostlen)) return NULL;
    const char* port=url+7+hostlen;
    if(port!=path) {
        if(*port++!=':'||port==path) return NULL;
        unsigned value=0;
        for(;port<path;port++) {
            if(*port<'0'||*port>'9') return NULL;
            value=value*10+(*port-'0'); if(value>65535) return NULL;
        }
        if(!value) return NULL;
    }
    memcpy(dest,url+7,n); dest[n]=0; return path;
}
static bool encode_name(const char* name,char* dest,size_t cap) {
    static const char digits[]="0123456789ABCDEF"; size_t n=0;
    for(const unsigned char* p=(const unsigned char*)name;*p;p++) {
        bool plain=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-._~",*p);
        size_t add=plain?1:3; if(n+add>=cap) return false;
        if(plain) dest[n++]=*p;
        else {dest[n++]='%';dest[n++]=digits[*p>>4];dest[n++]=digits[*p&15];}
    }
    dest[n]=0; return true;
}
bool sl_parse_http_install(const char* json,size_t size,const char* device_id,const char* sender,SlHttpInstall* out) {
    if(!json||!size||size>4096||!device_id||!sender||!out) return false;
    yyjson_doc* doc=yyjson_read(json,size,YYJSON_READ_NOFLAG); if(!doc) return false;
    yyjson_val* obj=yyjson_doc_get_root(doc); SlHttpInstall v={0};
    char type[32], device[33], origin_a[32], origin_b[32];
    bool valid=yyjson_is_obj(obj) && yyjson_obj_size(obj)==9
        &&text(obj,"type",type,sizeof(type))&&!strcmp(type,"switch-link-install")
        &&yyjson_is_uint(yyjson_obj_get(obj,"version"))&&yyjson_get_uint(yyjson_obj_get(obj,"version"))==1
        &&text(obj,"device_id",device,sizeof(device))&&hex(device,32)&&!strcmp(device,device_id)
        &&text(obj,"id",v.id,sizeof(v.id))&&hex(v.id,32)
        &&text(obj,"name",v.name,sizeof(v.name))&&filename(v.name)
        &&text(obj,"wire_name",v.wire_name,sizeof(v.wire_name))
        &&text(obj,"url",v.url,sizeof(v.url))&&text(obj,"result_url",v.result_url,sizeof(v.result_url));
    yyjson_val* count=yyjson_obj_get(obj,"size");
    valid=valid&&yyjson_is_uint(count);
    if(valid) {
        v.size=yyjson_get_uint(count); valid=v.size>0&&v.size<=(128ULL<<30);
        char expected[sizeof(v.wire_name)]; snprintf(expected,sizeof(expected),"%s_%s",v.id,v.name);
        valid=valid&&!strcmp(v.wire_name,expected);
    }
    if(valid) {
        const char* path=origin(v.url,sender,origin_a,sizeof(origin_a));
        const char* result=origin(v.result_url,sender,origin_b,sizeof(origin_b));
        valid=path&&result&&!strcmp(origin_a,origin_b);
        if(valid) {
            char prefix[64]; snprintf(prefix,sizeof(prefix),"/link-files/%s/",v.id);
            size_t n=strlen(prefix); valid=!strncmp(path,prefix,n)&&strlen(path+n)>65;
            char token[65]={0};
            if(valid) {
                memcpy(token,path+n,64); valid=hex(token,64)&&path[n+64]=='/';
                char encoded[601], expected[160];
                valid=valid&&encode_name(v.name,encoded,sizeof(encoded))&&!strcmp(path+n+65,encoded);
                snprintf(expected,sizeof(expected),"/link-result/%s/%s",v.id,token);
                valid=valid&&!strcmp(result,expected);
            }
        }
    }
    if(valid) *out=v;
    yyjson_doc_free(doc); return valid;
}
bool sl_http_install_can_accept(const char* id,const char* last_id,bool busy,bool ready) {
    return id&&last_id&&hex(id,32)&&strcmp(id,last_id)&&!busy&&ready;
}
bool sl_http_install_success(uint32_t result,unsigned titles) { return result==0&&titles>0; }
