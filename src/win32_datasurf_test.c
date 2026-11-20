#include "gitfluss.h"

#include <time.h>

u64 gfQueryMonotonic
(
    void
){
    static LARGE_INTEGER freq;
    static int initialized = 0;

    if (!initialized)
    {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    u64 seconds   = counter.QuadPart / freq.QuadPart;
    u64 remainder = counter.QuadPart % freq.QuadPart;

    u64 nanoseconds = (remainder * BILLION) / freq.QuadPart;

    return seconds * BILLION + nanoseconds;
}
