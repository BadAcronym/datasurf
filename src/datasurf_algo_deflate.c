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
}
DynHuffBlock;

uint8_t dynHuffCodelenghts[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

uint8_t bitmasks[8] =
{
    0x01,
    0x03,
    0x07,
    0x0F,
    0x1F,
    0x3F,
    0x7F,
    0xFF
};

f_internal uint16_t readBits
(
    const uint8_t *src,
    uint8_t       bitCount,
    uint8_t       *currBitOffset,
    uint64_t      *iterator
){
    PD_DEBUG("initial byte >> offset: %u", src[*iterator] >> *currBitOffset);
    if(bitCount + *currBitOffset < 8)
    {
        uint16_t value = src[*iterator] >> *currBitOffset & bitmasks[bitCount];
        PD_DEBUG("value read: %u", value);
        *currBitOffset += bitCount;
        PD_DEBUG("incrementing currBitOffset to %u.", *currBitOffset);
        return value;
    }

    PD_DEBUG("reading past next byte!");

    for(uint8_t i = 0; i < bitCount; i += 8)
    {
        if(*currBitOffset > 8)
        {
            *currBitOffset -= 8;
            ++*iterator;
        }
        bitCount -= 8;
    }

    return 0;
}

uint64_t dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       CINFO,
    uint8_t       FCHECK,
    uint8_t       FDICT,
    uint32_t      *checksum
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
                adlerA = (adlerA + dst[j]) % ADLER_PRIME;
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
            uint8_t currBitOffset = 0;

            DynHuffBlock dBlock = {0};

            // TESTING: just for testing the readBits function

            uint8_t expectedHLIT = src[i] >> 3;
            dBlock.HLIT = (uint8_t)readBits(src, 5, &currBitOffset, &i);
            if(dBlock.HLIT != expectedHLIT)
            {
                PD_ERROR("HLIT misread. Expected: %u, got: %u.",
                         expectedHLIT, dBlock.HLIT);
            }

            uint8_t expectedHDIST = src[i] & bitmasks[5];
            dBlock.HDIST = (uint8_t)readBits(src, 5, &currBitOffset, &i);
            if(dBlock.HDIST != expectedHDIST)
            {
                PD_ERROR("HDIST misread. Expected: %u, got: %u.",
                         expectedHDIST, dBlock.HDIST);
            }

            uint8_t expectedHCLEN = (src[i] >> 5) + src[i + 1] & 1;
            dBlock.HCLEN = (uint8_t)readBits(src, 4, &currBitOffset, &i);
            if(dBlock.HCLEN != expectedHCLEN)
            {
                PD_ERROR("HCLEN misread. Expected: %u, got: %u.",
                         expectedHCLEN, dBlock.HCLEN);
            }

            PD_DEBUG("HLIT:  %2u, actual: %3u", dBlock.HLIT,  dBlock.HLIT  + 257);
            PD_DEBUG("HDIST: %2u, actual: %3u", dBlock.HDIST, dBlock.HDIST + 1);
            PD_DEBUG("HCLEN: %2u, actual: %3u", dBlock.HCLEN, dBlock.HCLEN + 4);

            // read HCLEN + 4 number of codelengths, each being 3 bits.
            for(uint8_t j = 0; j < dBlock.HCLEN + 4; ++j)
            {
                uint8_t bits = (uint8_t)readBits(src, 3, &currBitOffset, &i);
                PD_DEBUG("codelength for %u: %u", dynHuffCodelenghts[j], bits);
            }

            PD_ERROR("dynamic huffman block not implemented.");
            return 0;
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            return 0;
        }

        ++src;
    }

    *checksum = (adlerB << 16) | adlerA;
    PD_DEBUG("read a total of %lu bytes.", i - 1);
    return i - 1;
}
