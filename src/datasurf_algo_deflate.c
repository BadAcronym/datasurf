#include "datasurf_main.h"
#include "datasurf_print_macros.h"

#define BTYPE_UNCROMPRESSED   0
#define BTYPE_STATIC_HUFFMAN  1
#define BTYPE_DYNAMIC_HUFFMAN 2
#define BTYPE_RESERVED        3

typedef struct DeflateBlock
{
    uint8_t BFINAL : 1;
    uint8_t BTYPE  : 2;
    uint8_t BDATA  : 5;
}
DeflateBlock;

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
            DS_DEBUG("BFINAL at byte %lu", i);
            break;
        }

        block.BTYPE = (src[i] >> 1) & 3;
        DS_DEBUG("BTYPE: %u", block.BTYPE);

        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            // 16 bits LEN
            // 16 bits NLEN
            // LEN bytes of actual, uncompressed data
            DS_ERROR("uncompressed block not implemented.");
            return false;
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            DS_ERROR("static huffman block not implemented.");
            return false;
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            DS_ERROR("dynamic huffman block not implemented.");
            return false;
        }
        else if(block.BTYPE == BTYPE_RESERVED)
        {
            DS_ERROR("BTYPE of 3 (bits 11) is reserved.");
            return false;
        }
    }

    return i;
}
