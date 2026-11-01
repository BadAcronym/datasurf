#ifndef DATASURF_HUFFMAN_HEADER
#define DATASURF_HUFFMAN_HEADER

#include "pd_int_types.h"

#define BTYPE_UNCROMPRESSED   0x00
#define BTYPE_STATIC_HUFFMAN  0x01
#define BTYPE_DYNAMIC_HUFFMAN 0x02
#define BTYPE_RESERVED        0x03

#define MAX_CODELEN           0x0F

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

extern u16 readBits
(
    const u08 *src,
    u08       bitCount,
    u08       *bitOffset,
    u64       *iterator
);

extern u16 reverseBits
(
    u16 code,
    u16 length
);

extern void makeCanonicalCodes
(
    const u16   *lengths,
    u16         symbolCount,
    HuffmanCode *codes
);

extern void insertCode
(
    HuffmanTree *tree,
    u16         code,
    u16         symbol,
    u16         length
);

extern void buildTree
(
    HuffmanTree *tree,
    const u16   *lengths,
    u16         symbolCount
);

extern void destroyTree
(
    HuffmanTree *tree
);

extern u16 decodeSymbol
(
    const HuffmanTree *tree,
    const u08         *src,
    u08               *currBitOffset,
    u64               *iterator
);

#endif
