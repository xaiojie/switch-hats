#include "log.hpp"
#include "fslock.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#endif

namespace papa {
namespace {

constexpr unsigned BuildVersionSeries = 5;
constexpr unsigned BuildVersionMinor = 1; // 与 CMakeLists 同步：0.5.1

// Producers only format into a bounded ring. They never acquire the output
// mutex or wait for a writer: saturation/contention drops diagnostic messages.
constexpr size_t QueueCapacity=128,LineCapacity=1400,BatchLines=16;
enum class LineKind {Message,Probe,Table};
struct Line {LineKind kind=LineKind::Message;char text[LineCapacity]{};};
std::array<Line,QueueCapacity> g_queue;
std::mutex g_queueMutex,g_outputMutex;
std::condition_variable g_changed;
size_t g_head=0,g_count=0;
std::atomic<bool> g_accepting{false},g_stop{false};
std::atomic<uint64_t> g_dropped{0};
std::atomic<bool>g_initialized{false};
bool g_threadReady=false;

#ifdef __SWITCH__
constexpr char LogPath[]="sdmc:/config/papa-walkman/log.txt";
constexpr char LogOldPath[]="sdmc:/config/papa-walkman/log.old.txt";
constexpr char TracePath[]="/config/papa-walkman/trace.txt";
constexpr long LogLimit=1024*1024,TraceLimit=512*1024;
u64 g_t0=0;
FsFileSystem g_ownedFs{};
FsFileSystem*g_traceDev=nullptr;
FsFile g_trace{};
std::atomic<bool> g_traceReady{false};
std::atomic<u64> g_traceOffset{0};
std::atomic<Result> g_traceResult{1};
FILE*g_file=nullptr;
bool g_fileBroken=false,g_traceOpen=false;
Thread g_thread{};
u64 Milliseconds(u64 now){const u64 freq=armGetSystemTickFreq();return freq?(now-g_t0)*1000ULL/freq:0;}

void TraceOpen(){
 FsFileSystem*dev=fsdevGetDeviceFileSystem("sdmc");
 if(!dev){Result rc=fsOpenSdCardFileSystem(&g_ownedFs);if(R_FAILED(rc)){g_traceResult=rc;return;}dev=&g_ownedFs;}
 g_traceDev=dev;fsFsCreateDirectory(dev,"/config");fsFsCreateDirectory(dev,"/config/papa-walkman");fsFsCreateFile(dev,TracePath,0,0);
 Result rc=fsFsOpenFile(dev,TracePath,FsOpenMode_Write|FsOpenMode_Append,&g_trace);
 if(R_FAILED(rc)){g_traceResult=rc;return;}
 s64 size=0;if(R_FAILED(fsFileGetSize(&g_trace,&size))||size<0)size=0;
 if(size>TraceLimit){
  fsFileClose(&g_trace);fsFsDeleteFile(dev,TracePath);fsFsCreateFile(dev,TracePath,0,0);
  rc=fsFsOpenFile(dev,TracePath,FsOpenMode_Write|FsOpenMode_Append,&g_trace);
  if(R_FAILED(rc)){g_traceResult=rc;return;}size=0;
 }
 g_traceOffset=u64(size);g_traceReady=true;g_traceOpen=true;g_traceResult=0;
}
void TraceWrite(const char*text,size_t len,bool flush){
 if(!g_traceReady.load())return;
 // Panic logging may race a normal batch. Reserve disjoint offsets without
 // taking any mutex on that emergency path.
 const u64 offset=g_traceOffset.fetch_add(len);
 Result rc=fsFileWrite(&g_trace,s64(offset),text,u64(len),flush?FsWriteOption_Flush:0);
 if(R_FAILED(rc)){g_traceReady=false;g_traceResult=rc;}
}
bool StdioUsable(){
 if(!g_file||g_fileBroken)return false;
 int fd=fileno(g_file);__handle*handle=fd>=3?__get_handle(fd):nullptr;
 if(!handle||handle->device>=32||!devoptab_list[handle->device]){g_fileBroken=true;return false;}
 return true;
}
std::string Diagnostic(const Line&line){
 char body[1024];
 if(line.kind==LineKind::Probe){
  int fd=g_file?fileno(g_file):-1;__handle*handle=fd>=0?__get_handle(fd):nullptr;
  unsigned dev=handle?handle->device:0xFFFFFFFFu;
  const devoptab_t*table=dev<32?devoptab_list[dev]:nullptr;
  std::snprintf(body,sizeof(body),"probe[%.256s] stdio_fd=%d handle=%p device=%u refs=%u devoptab=%p name=%s",
   line.text,fd,static_cast<void*>(handle),dev,handle?handle->refcount:0u,
   static_cast<const void*>(table),table&&table->name?table->name:"?");
 }else{
  char table[512]{};size_t used=0;
  for(unsigned i=0;i<32;++i){const devoptab_t*tab=devoptab_list[i];if(!tab)continue;
   int n=std::snprintf(table+used,sizeof(table)-used,"%u:%s ",i,tab->name?tab->name:"?");
   if(n<=0||size_t(n)>=sizeof(table)-used)break;
   used+=size_t(n);}
  std::snprintf(body,sizeof(body),"%.256s devoptab_list=%p [%s]",line.text,static_cast<const void*>(devoptab_list),table);
 }
 return body;
}
#else
std::thread g_thread;
#endif

void Enqueue(LineKind kind,const char*body){
 if(!g_accepting.load()){
#ifndef __SWITCH__
  // Portable callers need no initialization when using the ordinary stderr
  // logger. LogInit enables the same owned queue used on the device.
  if(!g_initialized)std::fprintf(stderr,"[papa] %s\n",body);
#endif
  return;
 }
 std::unique_lock<std::mutex>lock(g_queueMutex,std::try_to_lock);
 if(!lock.owns_lock()||g_count==QueueCapacity){++g_dropped;return;}
 if(!g_accepting.load())return;
 auto&line=g_queue[(g_head+g_count)%QueueCapacity];line.kind=kind;
#ifdef __SWITCH__
 if(kind==LineKind::Message)std::snprintf(line.text,sizeof(line.text),"[%9llu] %s\n",static_cast<unsigned long long>(Milliseconds(svcGetSystemTick())),body);
 else std::snprintf(line.text,sizeof(line.text),"%.256s",body);
#else
 std::snprintf(line.text,sizeof(line.text),"[papa] %s\n",body);
#endif
 ++g_count;lock.unlock();g_changed.notify_one();
}

bool DrainBatch(){
 std::array<Line,BatchLines> lines;size_t count=0;uint64_t dropped=0;
 {
  std::lock_guard<std::mutex>lock(g_queueMutex);
  while(g_count&&count<BatchLines){lines[count++]=g_queue[g_head];g_head=(g_head+1)%QueueCapacity;--g_count;}
  dropped=g_dropped.exchange(0);
 }
 if(!count&&!dropped)return false;
 // This lock is used only by the writer and shutdown, never by Logf.
 std::lock_guard<std::mutex>output(g_outputMutex);
 std::string batch;batch.reserve(count*LineCapacity+128);
 for(size_t i=0;i<count;++i){
#ifdef __SWITCH__
  if(lines[i].kind!=LineKind::Message){char text[LineCapacity];auto body=Diagnostic(lines[i]);
   std::snprintf(text,sizeof(text),"[%9llu] %s\n",static_cast<unsigned long long>(Milliseconds(svcGetSystemTick())),body.c_str());batch+=text;}
  else
#endif
  batch+=lines[i].text;
 }
 if(dropped){char text[128];std::snprintf(text,sizeof(text),"[papa] 日志队列已满或忙，省略 %llu 行\n",static_cast<unsigned long long>(dropped));batch+=text;}
#ifdef __SWITCH__
 // One storage flush per batch, instead of two flushes on every UI log call.
 TraceWrite(batch.data(),batch.size(),true);
 if(StdioUsable()&&(std::fwrite(batch.data(),1,batch.size(),g_file)!=batch.size()||std::fflush(g_file)!=0))g_fileBroken=true;
#else
 std::fwrite(batch.data(),1,batch.size(),stderr);std::fflush(stderr);
#endif
 return true;
}
void Writer(){
 for(;;){
  {
   std::unique_lock<std::mutex>lock(g_queueMutex);
   g_changed.wait_for(lock,std::chrono::milliseconds(100),[]{return g_stop.load()||g_count>=BatchLines;});
   if(g_stop.load()&&!g_count&&!g_dropped.load())break;
  }
  DrainBatch();
 }
}
#ifdef __SWITCH__
void WriterEntry(void*){Writer();}
#endif
} // namespace

void LogInit(){
 if(g_initialized)return;
 g_initialized=true;g_stop=false;g_accepting=true;
#ifdef __SWITCH__
 g_t0=svcGetSystemTick();g_fileBroken=false;
 mkdir("sdmc:/config",0777);mkdir("sdmc:/config/papa-walkman",0777);TraceOpen();
 if(FILE*old=OpenLocked(LogPath,"rb")){std::fseek(old,0,SEEK_END);long size=std::ftell(old);CloseLocked(old);
  if(size>LogLimit){RemoveLocked(LogOldPath);RenameLocked(LogPath,LogOldPath);}}
 g_file=OpenLocked(LogPath,"ab");if(!g_file)g_fileBroken=true;
 Logf("=== papa-walkman 0.%u.%u 诊断日志开始 tick=%llu ===",BuildVersionSeries,BuildVersionMinor,static_cast<unsigned long long>(g_t0));
 Logf("通道1(fs句柄) rc=0x%08x；通道2(stdio) %s",unsigned(g_traceResult.load()),g_file?"就绪":"打开失败");
#endif
 LogStartWatchdog();
}
void LogStartWatchdog(){
 if(!g_initialized||g_threadReady)return;
#ifdef __SWITCH__
 Result rc=threadCreate(&g_thread,WriterEntry,nullptr,nullptr,0x10000,0x32,-2);
 if(R_SUCCEEDED(rc)){rc=threadStart(&g_thread);if(R_FAILED(rc))threadClose(&g_thread);}
 g_threadReady=R_SUCCEEDED(rc);
 if(!g_threadReady){char line[128];std::snprintf(line,sizeof(line),"日志线程启动失败 rc=0x%08x",unsigned(rc));TraceRawLine(line);}
#else
 try{g_thread=std::thread(Writer);g_threadReady=true;}
 catch(const std::system_error&){g_threadReady=false;}
#endif
}
void LogClose(){
 if(!g_initialized)return;
 g_accepting=false;g_stop=true;g_changed.notify_all();
 if(g_threadReady){
#ifdef __SWITCH__
  threadWaitForExit(&g_thread);threadClose(&g_thread);
#else
  g_thread.join();
#endif
  g_threadReady=false;
 }
 // Also retains queued startup diagnostics if creating the writer failed.
 while(DrainBatch()){}
#ifdef __SWITCH__
 std::lock_guard<std::mutex>output(g_outputMutex);
 if(g_file){if(StdioUsable())std::fflush(g_file);CloseLocked(g_file);g_file=nullptr;}
 if(g_traceOpen){fsFileFlush(&g_trace);g_traceReady=false;fsFileClose(&g_trace);g_traceOpen=false;}
 // fsdev's mounted SD file system is shared and must never be closed here.
 if(g_traceDev==&g_ownedFs)fsFsClose(&g_ownedFs);
 g_traceDev=nullptr;
#endif
 g_initialized=false;
}
void Logf(const char*format,...){
 char body[1024];va_list args;va_start(args,format);std::vsnprintf(body,sizeof(body),format,args);va_end(args);
 Enqueue(LineKind::Message,body);
}
void LogProbe(const char*tag){Enqueue(LineKind::Probe,tag);}
void LogTable(const char*tag){Enqueue(LineKind::Table,tag);}
void TraceRawLine(const char*text){
#ifdef __SWITCH__
 char line[360];int n=std::snprintf(line,sizeof(line),"[%9llu] %s\n",static_cast<unsigned long long>(Milliseconds(svcGetSystemTick())),text);
 if(n>0)TraceWrite(line,size_t(n)<sizeof(line)?size_t(n):sizeof(line)-1,true);
#else
 std::fprintf(stderr,"[papa] %s\n",text);
#endif
}
void LogInstallExitHooks(){
#ifdef __SWITCH__
 std::set_terminate([]{TraceRawLine("TERMINATE 未捕获异常");std::abort();});
 std::atexit([]{TraceRawLine("ATEXIT 进程正常退出路径");});
#endif
}
} // namespace papa

#ifdef __SWITCH__
extern "C" void diagAbortWithResult(Result res){
 char line[96];std::snprintf(line,sizeof(line),"PANIC libnx result=0x%08x",static_cast<unsigned>(res));papa::TraceRawLine(line);
 svcBreak(BreakReason_Panic,reinterpret_cast<uintptr_t>(&res),sizeof(res));
 svcExitProcess();
}
#endif
