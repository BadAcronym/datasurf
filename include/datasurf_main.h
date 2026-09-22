#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static
#define s_global   static

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
// returns both the compressed amount of bytes read,
// as well as the decompressed amount of bytes produced.
extern DeflateInfo dsReadZlibPtr
(
    const uint8_t *zlib,
    uint8_t       *dest,
    uint64_t      cap
);

// returns both the compressed amount of bytes read,
// as well as the decompressed amount of bytes produced.
extern DeflateInfo dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint32_t      *checksum,
    uint64_t      cap
);

#endif
