#include "datasurf_main.h"
#include "pd_print_macros.h"

#define BTYPE_UNCROMPRESSED   0x00
#define BTYPE_STATIC_HUFFMAN  0x01
#define BTYPE_DYNAMIC_HUFFMAN 0x02
#define BTYPE_RESERVED        0x03

#define DS_DEFLATE_LOG

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
    uint8_t HDATA : 7; // not sure if 7
}
DynHuffBlock;

uint64_t dsReadDeflate
(
    uint8_t  *src,
    uint8_t  *dst,
    uint8_t  CINFO,
    uint8_t  FCHECK,
    uint8_t  FDICT,
    uint32_t *checksum
){
    uint32_t adlerA = 1;
    uint32_t adlerB = 0;

    bool endStream = false;

    DeflateBlock block = {0};

    uint64_t i = 0;
    for(; !endStream; ++i)
    {
        block.BFINAL = src[i] & 1;
        if(block.BFINAL)
        {
            PD_DEBUG("BFINAL found.");
            endStream = true;
        }

        block.BTYPE = src[i] >> 1;
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

            PD_DEBUG("identified LEN: %u bytes", LEN);

            if(NLEN != COMP)
            {
                PD_ERROR("NLEN does not match 1's complement of LEN: "
                         "LEN: %u, NLEN: %u, COMP: %u", LEN, NLEN, COMP);
                return 0;
            }

            for(uint32_t j = 0; j < LEN; ++j)
            {
                dst[j] = src[i];
                adlerA = (adlerA + src[i]) % ADLER_PRIME;
                adlerB = (adlerB + adlerA) % ADLER_PRIME;
                ++i;
            }
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            PD_ERROR("static huffman block not implemented.");
            return 0;
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            DynHuffBlock block = {0};
            block.HLIT  = src[i++] >> 3;
            block.HDIST = src[i];
            block.HCLEN = src[i++] >> 5;
            block.HCLEN += src[i];

            // data should start here, at src[i] >> 1

            PD_ERROR("dynamic huffman block not implemented.");
            return 0;
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            return 0;
        }
    }

    *checksum = (adlerB << 16) | adlerA;
    return i - 1;
}
