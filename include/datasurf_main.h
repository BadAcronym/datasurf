#ifndef DATASURF_H
#define DATASURF_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ADLER_PRIME 65521

extern bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest
);

extern uint64_t dsReadDeflate
(
    uint8_t  *src,
    uint8_t  *dst,
    uint8_t  CINFO,
    uint8_t  FCHECK,
    uint8_t  FDICT,
    uint32_t *checksum
);

#endif
