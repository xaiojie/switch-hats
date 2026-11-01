#pragma once
#include <cstdio>

namespace papa {

// devkitA64/newlib 的 fd 表是全局的，多线程同时 fopen/fclose 会拿到同一个 fd
// 并把它提前释放，导致后续 fclose 解引用空 devoptab（真机 Data Abort @0x18）。
// 所有打开/关闭/删除/改名都走这里，用一把全局递归锁串行化。
struct FsGuard {
  FsGuard();
  ~FsGuard();
  FsGuard(const FsGuard &) = delete;
  FsGuard &operator=(const FsGuard &) = delete;
};

FILE *OpenLocked(const char *path, const char *mode);
int CloseLocked(FILE *file);
int RemoveLocked(const char *path);
int RenameLocked(const char *from, const char *to);

} // namespace papa
