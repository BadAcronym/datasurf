#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define ADLER_PRIME 65521ULL

typedef struct DeflateInfo
{
    uint64_t compressedBytesRead;
    uint64_t bytesWritten;
}
DeflateInfo;

// `cap` is the actual cap on your output buffer size.
// returns the amount of DECOMPRESSED bytes that were written to buffer,
// not the compressed amount of bytes read.
extern uint64_t dsReadZlibPtr
(
    const uint8_t *zlib,
    uint8_t       *dest,
    uint64_t      cap
);

// returns the COMPRESSED amount of bytes read,
// not the uncompressed amount of bytes produced.
extern DeflateInfo dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint32_t      *checksum,
    uint64_t      cap
);

#endif
