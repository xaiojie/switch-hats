#include "fslock.hpp"

#include <mutex>

namespace papa {
namespace {
std::recursive_mutex &Mutex() {
  static std::recursive_mutex mutex;
  return mutex;
}
} // namespace

FsGuard::FsGuard() { Mutex().lock(); }
FsGuard::~FsGuard() { Mutex().unlock(); }

FILE *OpenLocked(const char *path, const char *mode) {
  FsGuard guard;
  return std::fopen(path, mode);
}

int CloseLocked(FILE *file) {
  if (!file)
    return 0;
  FsGuard guard;
  return std::fclose(file);
}

int RemoveLocked(const char *path) {
  FsGuard guard;
  return std::remove(path);
}

int RenameLocked(const char *from, const char *to) {
  FsGuard guard;
  return std::rename(from, to);
}

} // namespace papa
