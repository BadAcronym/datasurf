#include "datasurf_main.h"
#include "datasurf_huffman.h"

#include "pd_print_macros.h"

s_global const uint8_t dynHuffCodelenghOrder[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

s_global const uint16_t lengthBases[28] =
{
      3,   4,   5,   6,
      7,   8,   9,  10,
     11,  13,  15,  17,
     19,  23,  27,  31,
     35,  43,  51,  59,
     67,  83,  99, 115,
    131, 163, 195, 227,
};

s_global const uint8_t lengthExtraBits[28] =
{
    0, 0, 0, 0,
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
};

s_global const uint16_t distanceBases[30] =
{
     1,    2,    3,    4,     5,     7,
     9,    13,   17,   25,    33,    49,
     65,   97,   129,  193,   257,   385,
     513,  769,  1025, 1537,  2049,  3073,
     4097, 6145, 8193, 12289, 16385, 24577
};

s_global const uint8_t distanceExtraBits[30] =
{
    0,  0,  0,  0,  1,  1,
    2,  2,  3,  3,  4,  4,
    5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 10,
    11, 11, 12, 12, 13, 13
};

f_internal uint8_t *decodeHuffmanTrees
(
    HuffmanTree   *litLenTree,
    HuffmanTree   *distanceTree,
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       *og,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB,
    uint64_t      cap
){
    PD_TRACE("decoding max. %"PRIu64" symbols from huffman tree.", cap);

    for(uint64_t j = 0; j < cap; ++j)
    {
        uint16_t symbol = decodeSymbol(litLenTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 286, "a symbol of 286 or higher cannot be "
                  "interpreted for the literal/length tree.");

        if(symbol < 256)
        {
            // PD_TRACE("wrote literal: 0x%X", symbol);
            *dst    = (uint8_t)symbol;
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
            continue;
        }
        else if(symbol == 256)
        {
            return dst;
        }

        uint16_t length = 0;

        if(symbol < 265)
        {
            length = symbol - 254;
        }
        else if(symbol < 285)
        {
            uint16_t index     = symbol - 257;
            uint8_t  bitCount  = lengthExtraBits[index];
            uint32_t extraBits = 0;

            if(bitCount)
            {
                extraBits = readBits(src, bitCount, bitOffset, iterator);
            }

            length = (uint16_t)(lengthBases[index] + extraBits);
        }
        else // symbol == 285
        {
            length = 258;
        }

        symbol = decodeSymbol(distanceTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 30, "a symbol of 30 or higher cannot be "
                  "interpreted for the distance tree.");

        uint32_t distance = 0;

        if(symbol < 4)
        {
            distance = symbol + 1;
        }
        else // symbol < 30
        {
            uint8_t  bitCount  = distanceExtraBits[symbol];
            uint32_t extraBits = 0;

            if(bitCount)
            {
                extraBits = readBits(src, bitCount, bitOffset, iterator);
            }

            distance = distanceBases[symbol] + extraBits;
        }

        uint64_t produced = (uint64_t)(dst - og);

        if(distance > produced)
        {
            PD_ERROR("trying to go too far back: %"PRIu32" (max %"PRIu64").", distance,
                     produced);
            return 0;
        }
        else if(produced > cap)
        {
            PD_ERROR("output buffer overflow. wrote %"PRIu64", cap %"PRIu64".",
                     produced, cap);
            return 0;
        }
        else if(length + produced > cap)
        {
            PD_ERROR("output buffer overflow. length + produced %"PRIu64" too long, "
                     "max %"PRIu64".", length + produced, cap);
            return 0;
        }

        // PD_TRACE("LZ77: (length: %"PRIu32", distance: %"PRIu32")", length, distance);

        for(uint16_t l = 0; l < length; ++l)
        {
            // PD_TRACE("wrote LZ77: 0x%X <- byte #%"PRIu64", source byte #%"PRIu64"",
                     // *(dst - distance), (dst - og), (dst - og - distance));
            *dst    = *(dst - distance);
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
        }
    }

    return dst;
}

f_internal uint8_t *readBlock_nohuff
(
    const uint8_t *src,
    uint8_t       *dst,
    uint8_t       *og,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB,
    uint64_t      cap
){
    PD_TRACE("reading uncompressed block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    readBits(src, (8 - *bitOffset), bitOffset, iterator);
    uint16_t LEN  = readBits(src, 8, bitOffset, iterator);
    LEN += (readBits(src, 8, bitOffset, iterator) << 8);
    uint16_t NLEN = readBits(src, 8, bitOffset, iterator);
    NLEN += (readBits(src, 8, bitOffset, iterator) << 8);

    uint16_t COMP = LEN ^ 0xFFFF;

    PD_TRACE("identified LEN: %"PRIu32" bytes", LEN);

    if(!LEN)
    {
        PD_TRACE("skipping 0-length block.");
        return dst;
    }

    PD_ASSERT(LEN - 1 < cap - (uint64_t)(dst - og), "output buffer overflow. trying to "
              "read length %"PRIu32", max %"PRIu64".", LEN, cap - (uint64_t)(dst - og));

    if(LEN > cap - (uint64_t)(dst - og))
    {
        return 0;
    }

    if(NLEN != COMP)
    {
        PD_ERROR("NLEN does not match 1's complement of LEN: LEN: %"PRIu32", "
                 "NLEN: %"PRIu32", COMP: %"PRIu32"", LEN, NLEN, COMP);
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
    uint8_t       *og,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB,
    uint64_t      cap
){
    PD_TRACE("reading static block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    uint16_t litLenLengths[288] = {0};

    for(uint16_t i = 0; i < 144; ++i)
    {
        litLenLengths[i] = 8;
    }
    for(uint16_t i = 144; i < 256; ++i)
    {
        litLenLengths[i] = 9;
    }
    for(uint16_t i = 256; i < 280; ++i)
    {
        litLenLengths[i] = 7;
    }
    for(uint16_t i = 280; i < 288; ++i)
    {
        litLenLengths[i] = 8;
    }
    uint16_t distLengths[32] = {0};
    for(uint16_t i = 0; i < 32; ++i)
    {
        distLengths[i] = 5;
    }

    HuffmanTree literalLengthTree = {0};
    HuffmanTree distanceTree      = {0};

    buildTree(&literalLengthTree, litLenLengths, 288);
    buildTree(&distanceTree, distLengths, 32);

    dst = decodeHuffmanTrees(&literalLengthTree, &distanceTree, src, dst, og,
                             bitOffset, iterator, adlerA, adlerB, cap);

    destroyTree(&literalLengthTree);
    destroyTree(&distanceTree);

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
    uint32_t      *adlerB,
    uint64_t      cap
){
    PD_TRACE("reading dynamic block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    DynHuffBlock dBlock = {0};

    dBlock.HLIT  = (uint8_t)readBits(src, 5, bitOffset, iterator);
    dBlock.HDIST = (uint8_t)readBits(src, 5, bitOffset, iterator);
    dBlock.HCLEN = (uint8_t)readBits(src, 4, bitOffset, iterator);

    PD_TRACE("HLIT:  %2"PRIu16", actual: %3"PRIu16, dBlock.HLIT,  dBlock.HLIT  + 257);
    PD_TRACE("HDIST: %2"PRIu16", actual: %3"PRIu16, dBlock.HDIST, dBlock.HDIST + 1);
    PD_TRACE("HCLEN: %2"PRIu16", actual: %3"PRIu16, dBlock.HCLEN, dBlock.HCLEN + 4);
    PD_TRACE("read header at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

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

        PD_ASSERT(symbol < 19, "A symbol above 18 (%"PRIu32") from the compressed tree"
                  " cannot be interpreted.", symbol);

        if(symbol < 16)
        {
            // literal length
            litDistLengths[j] = (uint8_t)symbol;
            previousLength    = (uint8_t)symbol;
        }
        else if(symbol == 16)
        {
            // repeat previous length, 3-6 times
            uint8_t repeat = 3 + (uint8_t)readBits(src, 2, bitOffset, iterator);
            for(uint8_t k = 0; k < repeat; ++k)
            {
                PD_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
                          " exceeds maximum of %"PRIu32".", j, totalLength)
                litDistLengths[j + k] = previousLength;
            }
            j += repeat - 1;
        }
        else if(symbol == 17)
        {
            // repeat zero, 3-10 times
            uint8_t repeat = 3 + (uint8_t)readBits(src, 3, bitOffset, iterator);
            for(uint8_t k = 0; k < repeat; ++k)
            {
                PD_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
                          " exceeds maximum of %"PRIu32".", j, totalLength)
                litDistLengths[j + k] = 0;
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
                PD_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
                          " exceeds maximum of %"PRIu32".", j, totalLength)
                litDistLengths[j + k] = 0;
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

    // with the two trees constructed, we can go through them and finally decode
    // the data! since it's actually interleaved, like this:
    // ...
    // literal
    // length + distance
    // literal
    // ...

    dst = decodeHuffmanTrees(&literalLengthTree, &distanceTree, src, dst, og,
                             bitOffset, iterator, adlerA, adlerB, cap);

    destroyTree(&literalLengthTree);
    destroyTree(&distanceTree);
    destroyTree(&encodedTree);

    return dst;
}

DeflateInfo dsReadDeflate
(
    const uint8_t *src,
    uint8_t       *dst,
    uint32_t      *checksum,
    uint64_t      cap
){
    uint8_t *og = dst;

    uint8_t  bitOffset = 0;
    uint32_t adlerA    = 1;
    uint32_t adlerB    = 0;

    DeflateInfo  resultInfo = {0};
    DeflateBlock block      = {0};

    uint64_t i = 0;
    bool endStream = false;
    while(!endStream)
    {
        PD_TRACE("starting new block at byte %"PRIu64", bit offset %"PRIu8".",
                 i, bitOffset);
        block.BFINAL = (uint8_t)readBits(src, 1, &bitOffset, &i);
        if(block.BFINAL)
        {
            PD_TRACE("BFINAL found.");
            endStream = true;
        }

        block.BTYPE = (uint8_t)readBits(src, 2, &bitOffset, &i);
        PD_TRACE("BTYPE: %"PRIu8"", block.BTYPE);

        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            uint8_t *start = dst;
            dst = readBlock_nohuff(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            if(!dst)
            {
                PD_ERROR("failed to read uncompressed huffman block.");
                break;
            }

            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            uint8_t *start = dst;
            dst = readBlock_static(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            if(!dst)
            {
                PD_ERROR("failed to read static huffman block.");
                goto error;
            }

            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            uint8_t *start = dst;
            dst = readBlock_dynamic(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            if(!dst)
            {
                PD_ERROR("failed to read dynamic huffman block.");
                goto error;
            }

            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            goto error;
        }
        PD_TRACE("ending deflate block at byte index %"PRIu64", bit offset %"PRIu8".",
                 i, bitOffset);
    }

    *checksum = (adlerB << 16) | adlerA;
    resultInfo.success = true;

    if(bitOffset)
    {
        ++i;
    }

    PD_TRACE("ending deflate stream at byte index %"PRIu64", bit offset %"PRIu8".",
             i, bitOffset);

    *checksum = (adlerB << 16) | adlerA;
    resultInfo.success   = true;
    resultInfo.bytesRead = i;
    return resultInfo;

error:
    return resultInfo;
}
