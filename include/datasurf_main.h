#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ADLER_PRIME 65521ULL

extern bool dsReadZlibPtr
(
    const uint8_t *zlib,
    uint8_t       *dest
);

// returns the COMPRESSED amount of bytes read,
// not the uncompressed amount of bytes produced.
extern uint64_t dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       CINFO,
    uint8_t       FCHECK,
    uint8_t       FDICT,
    uint32_t      *checksum
);

#endif
