// Model libnx: pthread_detach has no implementation and returns ENOSYS.
// Run the real HTTP tests with this interposition to catch accidental detach.
#include <cerrno>
#include <pthread.h>

#ifdef __APPLE__
static int UnsupportedDetach(pthread_t) { return ENOSYS; }
__attribute__((used)) static const struct {
    int (*replacement)(pthread_t);
    int (*original)(pthread_t);
} interpose __attribute__((section("__DATA,__interpose"))) = {
    UnsupportedDetach, pthread_detach
};
#else
extern "C" int pthread_detach(pthread_t) noexcept { return ENOSYS; }
#endif
