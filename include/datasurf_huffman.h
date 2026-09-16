#ifndef DATASURF_HUFFMAN_HEADER
#define DATASURF_HUFFMAN_HEADER

#define BTYPE_UNCROMPRESSED   0x00
#define BTYPE_STATIC_HUFFMAN  0x01
#define BTYPE_DYNAMIC_HUFFMAN 0x02
#define BTYPE_RESERVED        0x03

#define MAX_CODELEN           0x0F

#include <stdint.h>

typedef struct DeflateBlock
{
    uint8_t BFINAL : 1;
    uint8_t BTYPE  : 2;
    uint8_t BDATA  : 5;
}
DeflateBlock;

typedef struct DynHuffBlock
{
    uint8_t HLIT  : 5;
    uint8_t HDIST : 5;
    uint8_t HCLEN : 4;
}
DynHuffBlock;

typedef struct HuffmanCode
{
    uint16_t code;
    uint16_t length;
    uint16_t symbol;
}
HuffmanCode;

typedef struct HuffmanNode
{
    int16_t symbol;
    int16_t children[2];
}
HuffmanNode;

typedef struct HuffmanTree
{
    HuffmanNode *nodes;
    uint16_t    nodeCount;
}
HuffmanTree;

#endif
