#include "timeutil.h"
#include <errno.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

void sleep_ms(long ms) {
    if (ms <= 0) {
        return;
    }
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    struct timespec remaining;
    while (nanosleep(&ts, &remaining) == -1 && errno == EINTR) {
        ts = remaining;
    }
#endif
}
