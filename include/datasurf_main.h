#ifndef DATASURF_H
#define DATASURF_H

#define f_internal static

#include "datasurf_huffman.h"

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
    uint32_t      *checksum
);

extern uint16_t readBits
(
    const uint8_t *src,
    uint8_t       bitCount,
    uint8_t       *bitOffset,
    uint64_t      *iterator
);

extern uint16_t reverseBits
(
    uint16_t code,
    uint16_t length
);

extern void makeCanonicalCodes
(
    const uint16_t *lengths,
    uint16_t       symbolCount,
    HuffmanCode    *codes
);

extern void insertCode
(
    HuffmanTree *tree,
    uint16_t    code,
    uint16_t    symbol,
    uint16_t    length
);

extern void buildTree
(
    HuffmanTree    *tree,
    const uint16_t *lengths,
    uint16_t       symbolCount
);

extern uint16_t decodeSymbol
(
    const HuffmanTree *tree,
    const uint8_t     *src,
    uint8_t           *currBitOffset,
    uint64_t          *iterator
);

#endif
