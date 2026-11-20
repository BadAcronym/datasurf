#include "datasurf_main.h"

#include <time.h>

u64 dsQueryMonotonic
(
    void
){
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);

    return (u64)spec.tv_sec * BILLION + (u64)spec.tv_nsec;
}
