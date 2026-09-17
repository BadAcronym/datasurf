#include "datasurf_main.h"
#include "datasurf_huffman.h"

#include "pd_print_macros.h"

const uint8_t dynHuffCodelenghOrder[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

f_internal uint8_t *readBlock_uncompressed
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB
){
    // skip to next byte
    *bitOffset = 0;
    *iterator += 1;
    uint16_t LEN  = src[*iterator] + (uint16_t)(src[*iterator + 1] << 8);
    *iterator += 2;
    uint16_t NLEN = src[*iterator] + (uint16_t)(src[*iterator + 1] << 8);
    *iterator += 2;
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
        *dst    = src[*iterator];
        *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
        *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
        *iterator += 1;
    }

    return dst;
}

f_internal uint8_t *readBlock_static
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB
){
    // for now
    PD_ERROR("static huffman block not implemented.");
    return 0;
    //

    return dst;
}

f_internal uint8_t *readBlock_dynamic
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       *og,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB
){
    DynHuffBlock dBlock = {0};

    dBlock.HLIT  = (uint8_t)readBits(src, 5, bitOffset, iterator);
    dBlock.HDIST = (uint8_t)readBits(src, 5, bitOffset, iterator);
    dBlock.HCLEN = (uint8_t)readBits(src, 4, bitOffset, iterator);

    PD_DEBUG("HLIT:  %2u, actual: %3u", dBlock.HLIT,  dBlock.HLIT  + 257);
    PD_DEBUG("HDIST: %2u, actual: %3u", dBlock.HDIST, dBlock.HDIST + 1);
    PD_DEBUG("HCLEN: %2u, actual: %3u", dBlock.HCLEN, dBlock.HCLEN + 4);

    uint16_t compressLengths[19] = {0};

    // read HCLEN + 4 number of codelengths, each being 3 bits.
    for(uint8_t j = 0; j < dBlock.HCLEN + 4; ++j)
    {
        uint8_t symbol = dynHuffCodelenghOrder[j];
        compressLengths[symbol] = (uint8_t)readBits(src, 3, bitOffset, iterator);
    }

    uint16_t litLenTreeLength = dBlock.HLIT  + 257;
    uint8_t  distTreeLength   = dBlock.HDIST + 1;
    uint16_t totalLength      = litLenTreeLength + distTreeLength;
    uint16_t litDistLengths[totalLength];
    uint16_t previousLength = 0;

    // I now need to model and construct the "canonical" huffman tree
    // using the lengths in compressLengths, before I can obtain the
    // codes for the other two trees.
    HuffmanTree encodedTree = {0};
    buildTree(&encodedTree, compressLengths, 19);

    for(uint16_t j = 0; j < totalLength; ++j)
    {
        uint16_t symbol = decodeSymbol(&encodedTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 19, "A symbol above 18 (%u) from the compressed tree"
                  " cannot be interpreted.", symbol);

        if(symbol < 16)
        {
            // literal length
            litDistLengths[j] = (uint8_t)symbol;
            previousLength    = (uint8_t)symbol;
            PD_ASSERT(j < totalLength, "index into litDistLengths "
                      "%u exceeds maximum of %u.", j, totalLength)
        }
        else if(symbol == 16)
        {
            // repeat previous length, 3-6 times
            uint8_t repeat = 3 + (uint8_t)readBits(src, 2, bitOffset, iterator);
            for(uint8_t k = 0; k < repeat; ++k)
            {
                litDistLengths[j + k] = previousLength;
                PD_ASSERT(j + k < totalLength, "index into litDistLengths "
                          "%u exceeds maximum of %u.", j, totalLength)
            }
            j += repeat - 1;
        }
        else if(symbol == 17)
        {
            // repeat zero, 3-10 times
            uint8_t repeat = 3 + (uint8_t)readBits(src, 3, bitOffset, iterator);
            for(uint8_t k = 0; k < repeat; ++k)
            {
                litDistLengths[j + k] = 0;
                PD_ASSERT(j + k < totalLength, "index into litDistLengths "
                          "%u exceeds maximum of %u.", j, totalLength)
            }
            j += repeat - 1;
            previousLength = 0;
        }
        else if(symbol == 18)
        {
            // repeat zero, 11-138 times.
            uint8_t repeat = 11 + (uint8_t)readBits(src, 7, bitOffset, iterator);
            for(uint8_t k = 0; k < repeat; ++k)
            {
                litDistLengths[j + k] = 0;
                PD_ASSERT(j + k < totalLength, "index into litDistLengths "
                          "%u exceeds maximum of %u.", j, totalLength)
            }
            j += repeat - 1;
            previousLength = 0;
        }
    }

    HuffmanTree literalLengthTree = {0};
    HuffmanTree distanceTree      = {0};
    buildTree(&literalLengthTree, litDistLengths, litLenTreeLength);
    buildTree(&distanceTree, &litDistLengths[litLenTreeLength],
              distTreeLength);

    bool endBlock = false;

    // with the two trees constructed, we can go through them and finally decode
    // the data! since it's actually interleaved, like this:
    // ...
    // literal
    // length + distance
    // literal
    // ...

    for(uint16_t j = 0; !endBlock && j < totalLength; ++j)
    {
        uint16_t symbol = decodeSymbol(&literalLengthTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 286, "a symbol of 286 or higher cannot be "
                  "interpreted for the literal/length tree.");

        if(symbol < 256)
        {
            *dst    = (uint8_t)symbol;
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
            continue;
        }
        else if(symbol == 256)
        {
            PD_DEBUG("ending dynamic huffman block.");
            endBlock = true;
            break;
        }

        uint16_t length = 0;

        if(symbol < 265)
        {
            length = symbol - 254;
        }
        else if(symbol < 269)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 1, bitOffset, iterator);
            length = 11 + extraBits + 2 * (symbol - 265);
        }
        else if(symbol < 273)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 2, bitOffset, iterator);
            length = 19 + extraBits + 4 * (symbol - 269);
        }
        else if(symbol < 277)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 3, bitOffset, iterator);
            length = 35 + extraBits + 8 * (symbol - 273);
        }
        else if(symbol < 281)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 4, bitOffset, iterator);
            length = 67 + extraBits + 16 * (symbol - 277);
        }
        else if(symbol < 285)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 5, bitOffset, iterator);
            length = 131 + extraBits + 32 * (symbol - 281);
        }
        else // symbol == 285
        {
            length = 258;
        }

        symbol = decodeSymbol(&distanceTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 30, "a symbol of 30 or higher cannot be "
                  "interpreted for the distance tree.");

        uint32_t distance = 0;

        if(symbol < 4)
        {
            distance = symbol + 1;
        }
        else if(symbol < 6)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 1, bitOffset, iterator);
            distance = 5 + extraBits + 2 * (symbol - 4);
        }
        else if(symbol < 8)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 2, bitOffset, iterator);
            distance = 9 + extraBits + 4 * (symbol - 6);
        }
        else if(symbol < 10)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 3, bitOffset, iterator);
            distance = 17 + extraBits + 8 * (symbol - 8);
        }
        else if(symbol < 12)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 4, bitOffset, iterator);
            distance = 33 + extraBits + 16 * (symbol - 10);
        }
        else if(symbol < 14)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 5, bitOffset, iterator);
            distance = 65 + extraBits + 32 * (symbol - 12);
        }
        else if(symbol < 16)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 6, bitOffset, iterator);
            distance = 129 + extraBits + 64 * (symbol - 14);
        }
        else if(symbol < 18)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 7, bitOffset, iterator);
            distance = 257 + extraBits + 128 * (symbol - 16);
        }
        else if(symbol < 20)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 8, bitOffset, iterator);
            distance = 513 + extraBits + 256 * (symbol - 18);
        }
        else if(symbol < 22)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 9, bitOffset, iterator);
            distance = 1025 + extraBits + 512 * (symbol - 20);
        }
        else if(symbol < 24)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 10, bitOffset, iterator);
            distance = 2049 + extraBits + 1024 * (symbol - 22);
        }
        else if(symbol < 26)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 11, bitOffset, iterator);
            distance = 4097 + extraBits + 2048 * (symbol - 24);
        }
        else if(symbol < 28)
        {
            uint8_t extraBits = (uint8_t)readBits(src, 12, bitOffset, iterator);
            distance = 8193 + extraBits + 4096 * (symbol - 26);
        }
        else // symbol < 30
        {
            uint8_t extraBits = (uint8_t)readBits(src, 13, bitOffset, iterator);
            distance  = 16385 + extraBits + 8192 * (symbol - 28);
        }

        PD_ASSERT(distance - 1 < dst - og, "trying to go too far back: %u "
                  "(max %lu).", distance, (uint64_t)(dst - og));

        for(uint16_t l = 0; l < length; ++l)
        {
            *dst    = *(dst - distance);
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
        }
    }

    destroyTree(&literalLengthTree);
    destroyTree(&distanceTree);
    destroyTree(&encodedTree);

    return dst;
}

uint64_t dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint32_t      *checksum
){
    uint8_t *og = dst;

    uint8_t  bitOffset = 0;
    uint32_t adlerA    = 1;
    uint32_t adlerB    = 0;

    bool endStream = false;

    DeflateBlock block = {0};

    uint64_t i = 0;
    for(; !endStream; ++i)
    {
        block.BFINAL = (uint8_t)readBits(src, 1, &bitOffset, &i);
        if(block.BFINAL)
        {
            PD_DEBUG("BFINAL found.");
            endStream = true;
        }

        block.BTYPE = (uint8_t)readBits(src, 2, &bitOffset, &i);
        PD_DEBUG("BTYPE: %u", block.BTYPE);

        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            dst = readBlock_uncompressed(src, dst, &bitOffset, &i, &adlerA, &adlerB);
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            dst = readBlock_static(src, dst, &bitOffset, &i, &adlerA, &adlerB);
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            dst = readBlock_dynamic(src, dst, og, &bitOffset, &i, &adlerA, &adlerB);
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            return 0;
        }

        if(!dst)
        {
            return 0;
        }
    }

    if(bitOffset)
    {
        ++i;
    }

    *checksum = (adlerB << 16) | adlerA;
    return i - 1;
}
