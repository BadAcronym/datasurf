#include "datasurf_main.h"

#include <stdio.h>

typedef struct DeflateBlock
{
    uint8_t BFINAL : 1;
    uint8_t BTYPE  : 2;
    uint8_t BDATA  : 5;
}
DeflateBlock;

#define BTYPE_UNCROMPRESSED   0
#define BTYPE_STATIC_HUFFMAN  1
#define BTYPE_DYNAMIC_HUFFMAN 2
#define BTYPE_RESERVED        3

uint64_t dsReadDeflate
(
    uint8_t  *src,
    uint8_t  *dst,
    uint8_t  CINFO,
    uint8_t  FCHECK,
    uint8_t  FDICT,
    uint32_t *checksum
){
    bool endStream = false;

    DeflateBlock block = {0};

    uint64_t i = 0;
    for(; !endStream; ++i)
    {
        block.BFINAL = src[i] & 1;
        if(block.BFINAL)
        {
            #ifdef DEBUG
            fprintf(stderr, "BFINAL at byte %lu\n", i);
            #endif
            break;
        }

        block.BTYPE = (src[i] >> 1) & 3;
        #ifdef DEBUG
        fprintf(stderr, "BTYPE: %u\n", block.BTYPE);
        #endif
        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            // 16 bits LEN
            // 16 bits NLEN
            // LEN bytes of actual, uncompressed data
            fprintf(stderr, "\033[31;3mERROR: uncompressed block not implemented."
                    "\033[0m\n");
            return false;
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            fprintf(stderr, "\033[31;3mERROR: static huffman block not implemented."
                    "\033[0m\n");
            return false;
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            fprintf(stderr, "\033[31;3mERROR: dynamic huffman block not implemented."
                    "\033[0m\n");
            return false;
        }
        else if(block.BTYPE == BTYPE_RESERVED)
        {
            fprintf(stderr, "\033[31;3mERROR: BTYPE of 3 (bits 11) is reserved."
                    "\033[0m\n");
            return false;
        }
    }

    return i;
}
