#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static
#define s_global   static

#include "pd_int_types.h"

#define ADLER_PRIME 65521ULL

typedef struct DeflateInfo
{
    u64  bytesRead;
    u64  bytesWritten;
    bool doneReading;
    bool success;
}
DeflateInfo;

// `cap` is the actual cap on your output buffer size.
// returns both the compressed amount of bytes read,
// as well as the decompressed amount of bytes produced.
// if `cap` bytes are filled, but there is still more data
// to read, `doneReading` will be false.
extern DeflateInfo dsReadZlibPtr
(
    const u08 *zlib,
    u08       *dest,
    u64       cap
);

// like the simple dsReadZlibPtrScratch, but will only work
// with a scratch buffer of `bufsize` bytes.
extern DeflateInfo dsReadZlibPtrScratch
(
    const u08 *zlib,
    u08       *dest,
    u64       bufsize,
    u64       cap
);

// returns both the compressed amount of bytes read,
// as well as the decompressed amount of bytes produced.
extern DeflateInfo dsReadDeflate
(
    const u08 *src,
    u08       *dst,
    u32       *checksum,
    u64       cap
);

#endif
