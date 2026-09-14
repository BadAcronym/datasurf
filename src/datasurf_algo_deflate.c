#include "datasurf_main.h"
#include "pd_print_macros.h"

#define BTYPE_UNCROMPRESSED   0x00
#define BTYPE_STATIC_HUFFMAN  0x01
#define BTYPE_DYNAMIC_HUFFMAN 0x02
#define BTYPE_RESERVED        0x03

#define MAX_CODELEN           0x0F

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

typedef struct HuffmanCode
{
    uint16_t code;
    uint8_t  length;
    uint16_t symbol;
}
HuffmanCode;

typedef struct HuffmanNode
{
    uint16_t symbol;
    uint16_t children[2];
}
HuffmanNode;

typedef struct HuffmanTree
{
    HuffmanNode *nodes;
    uint16_t    nodeCount;
}
HuffmanTree;

uint8_t dynHuffCodelenghOrder[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

uint8_t bitmasks[9] =
{
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

f_internal uint16_t readBits
(
    const uint8_t *src,
    uint8_t       bitCount,
    uint8_t       *currBitOffset,
    uint64_t      *iterator
){
    PD_ASSERT(*currBitOffset < 8, "currBitOffset cannot be bigger than 7.");
    PD_ASSERT(bitCount < 16, "maximum bit count to be read is 16.");

    uint16_t value    = 0;
    uint8_t  bitsRead = 0;

    while(bitCount > 0)
    {
        uint8_t  available = 8 - *currBitOffset;
        uint8_t  take      = bitCount < available ? bitCount : available;
        uint16_t part      = (src[*iterator] >> *currBitOffset) & bitmasks[take];

        value          |= part << bitsRead;
        bitsRead       += take;
        bitCount       -= take;
        *currBitOffset += take;

        if(*currBitOffset > 7)
        {
            *currBitOffset = 0;
            ++(*iterator);
        }
    }

    return value;
}

f_internal uint16_t decodeSymbol
(
    const HuffmanTree *tree,
    const uint8_t     *src,
    uint8_t           *currBitOffset,
    uint64_t          *iterator
){
    PD_ASSERT(*currBitOffset < 8, "currBitOffset cannot be bigger than 7.");

    uint16_t symbol = 0;

    // read bits until the constructed code matches a value in the huffman tree that's
    // used to read the other two huffman trees.
    bool match = false;
    while(!match)
    {
        uint8_t bit = (uint8_t)readBits(src, 1, currBitOffset, iterator);
    }

    return symbol;
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
            uint8_t      currBitOffset = 0;
            DynHuffBlock dBlock        = {0};

            dBlock.HLIT  = (uint8_t)readBits(src, 5, &currBitOffset, &i);
            dBlock.HDIST = (uint8_t)readBits(src, 5, &currBitOffset, &i);
            dBlock.HCLEN = (uint8_t)readBits(src, 4, &currBitOffset, &i);

            PD_DEBUG("HLIT:  %2u, actual: %3u", dBlock.HLIT,  dBlock.HLIT  + 257);
            PD_DEBUG("HDIST: %2u, actual: %3u", dBlock.HDIST, dBlock.HDIST + 1);
            PD_DEBUG("HCLEN: %2u, actual: %3u", dBlock.HCLEN, dBlock.HCLEN + 4);

            uint8_t compressLengths[19] = {0};

            // read HCLEN + 4 number of codelengths, each being 3 bits.
            for(uint8_t j = 0; j < dBlock.HCLEN + 4; ++j)
            {
                uint8_t symbol = dynHuffCodelenghOrder[j];
                compressLengths[symbol] = (uint8_t)readBits(src, 3, &currBitOffset, &i);
                PD_DEBUG("read code length %u for symbol %u.",
                         compressLengths[symbol], symbol);
            }

            uint16_t distanceLengthOffset = dBlock.HDIST + 1;
            uint16_t totalLength = dBlock.HLIT + dBlock.HDIST + 258;
            uint16_t litDistLengths[totalLength];
            uint16_t previousLength = 0;

            // I somehow need to model and construct the "canonical" huffman tree using
            // the data above, before I can proceed.

            for(uint16_t j = 0; j < totalLength; ++j)
            {
                uint16_t symbol = decodeSymbol(src, &currBitOffset, &i);

                PD_ASSERT(symbol < 18, "A symbol above 18 cannot be interpreted.");

                if(symbol < 16)
                {
                    litDistLengths[j] = (uint8_t)symbol;
                    previousLength    = (uint8_t)symbol;
                }
                else if(symbol == 16)
                {
                    // repeat previous length, 3-6 times
                }
                else if(symbol == 17)
                {
                    // repeat zero, 3-10 times
                }
                else if(symbol == 18)
                {
                    // repeat zero, 11-138 times.
                }
            }
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
