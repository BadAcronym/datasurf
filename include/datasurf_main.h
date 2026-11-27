#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static
#define s_global   static
#define BILLION    1000000000L

#define DS_LIKELY(c)   __builtin_expect(!!(c), 1)
#define DS_UNLIKELY(c) __builtin_expect(!!(c), 0)

#include "pd_int_types.h"

#define ADLER_PRIME 65521ULL

#define BTYPE_UNCROMPRESSED   0x00
#define BTYPE_STATIC_HUFFMAN  0x01
#define BTYPE_DYNAMIC_HUFFMAN 0x02
#define BTYPE_RESERVED        0x03

#define MAX_CODELEN           0x0F

// contains both the compressed amount of bytes read,
// as well as the decompressed amount of bytes produced.
// we might succeed in reading less than the total amount
// of bytes that could be decoded, hence success != done.
typedef struct DeflateInfo
{
    u64  cursor;
    u64  bytesRead;
    u64  bytesWritten;
    bool doneReading;
    bool success;
}
DeflateInfo;

typedef struct DeflateBlock
{
    u08 BFINAL : 1;
    u08 BTYPE  : 2;
    u08 BDATA  : 5;
}
DeflateBlock;

typedef struct DynHuffBlock
{
    u08 HLIT  : 5;
    u08 HDIST : 5;
    u08 HCLEN : 4;
}
DynHuffBlock;

typedef struct HuffmanCode
{
    u16 code;
    u16 length;
    u16 symbol;
}
HuffmanCode;

typedef struct HuffmanNode
{
    i16 symbol;
    i32 children[2];
}
HuffmanNode;

typedef struct HuffmanTree
{
    HuffmanNode *nodes;
    u16         nodeCount;
}
HuffmanTree;

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

extern DeflateInfo dsReadDeflate
(
    const u08 *src,
    u08       *dst,
    u32       *checksum,
    u64       cap
);

extern u64 dsQueryMonotonic
(
    void
);

#endif
