#include "datasurf_main.h"
#include "string_view.h"
#include "pd_print_macros.h"

#include <memory.h>

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
        block.BFINAL = src[i] >> 7;
        if(block.BFINAL)
        {
            PD_DEBUG("BFINAL at byte %lu", i);
            endStream = true;
        }

        block.BTYPE = src[i] >> 5;
        PD_DEBUG("BTYPE: %u", block.BTYPE);

        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            // skip to next byte
            ++i;
            uint16_t LEN  = src[i] + (uint16_t)(src[i + 1] << 8);
            i += 2;
            uint16_t NLEN = src[i] + (uint16_t)(src[i + 1] << 8);
            i += 2;
            uint16_t COMP = LEN ^ 65535;

            PD_DEBUG("identified uncompressed block.");
            PD_DEBUG("identified LEN: %u bytes", LEN);

            if(NLEN != COMP)
            {
                PD_ERROR("NLEN does not match 1's complement of LEN: "
                         "LEN: %u, NLEN: %u, COMP: %u", LEN, NLEN, COMP);
                return 0;
            }

            memcpy(dst, src + i, LEN);
            StringView test = {0};
            test.data = (char*)dst;
            test.size = LEN;

            PD_DEBUG("data: " PRI_SV, ARG_SV(test));

            i += LEN;
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            PD_ERROR("static huffman block not implemented.");
            return 0;
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            PD_ERROR("dynamic huffman block not implemented.");
            return 0;
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 (bits 11) is reserved.");
            return 0;
        }
    }

    return i;
}
