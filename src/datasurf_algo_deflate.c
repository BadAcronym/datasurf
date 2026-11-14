#include "datasurf_main.h"
#include "ds_print_macros.h"
#include "datasurf_huffman.h"

#include <inttypes.h>

#define DS_END_EARLY ((u08*)1)

s_global const u08 dynHuffCodelenghOrder[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

s_global const u16 lengthBases[28] =
{
      3,   4,   5,   6,
      7,   8,   9,  10,
     11,  13,  15,  17,
     19,  23,  27,  31,
     35,  43,  51,  59,
     67,  83,  99, 115,
    131, 163, 195, 227,
};

s_global const u08 lengthExtraBits[28] =
{
    0, 0, 0, 0,
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    5, 5, 5, 5,
};

s_global const u16 distanceBases[30] =
{
     1,    2,    3,    4,     5,     7,
     9,    13,   17,   25,    33,    49,
     65,   97,   129,  193,   257,   385,
     513,  769,  1025, 1537,  2049,  3073,
     4097, 6145, 8193, 12289, 16385, 24577
};

s_global const u08 distanceExtraBits[30] =
{
    0,  0,  0,  0,  1,  1,
    2,  2,  3,  3,  4,  4,
    5,  5,  6,  6,  7,  7,
    8,  8,  9,  9,  10, 10,
    11, 11, 12, 12, 13, 13
};

f_internal u08 *decodeHuffmanTrees
(
    DeflateInfo *info,
    HuffmanTree *litLenTree,
    HuffmanTree *distanceTree,
    const u08   *src,
    u08         *dst,
    u08         *og,
    u08         *bitOffset,
    u64         *iterator,
    u32         *adlerA,
    u32         *adlerB,
    u64         cap
){
    DS_TRACE("decoding max. %"PRIu64" symbols from huffman tree.", cap);

    for(;;)
    {
        u16 symbol = decodeSymbol(litLenTree, src, bitOffset, iterator);

        DS_ASSERT(symbol < 286, "a symbol of 286 or higher cannot be "
                  "interpreted for the literal/length tree.");

        if(symbol < 256)
        {
            // DS_TRACE("wrote literal: 0x%X", symbol);
            *dst    = (u08)symbol;
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;

            if(info->bytesWritten >= cap)
            {
                return DS_END_EARLY;
            }
            ++info->bytesWritten;

            continue;
        }
        else if(symbol == 256)
        {
            return dst;
        }

        u16 length = 0;

        if(symbol < 265)
        {
            length = symbol - 254;
        }
        else if(symbol < 285)
        {
            u16 index     = symbol - 257;
            u08 bitCount  = lengthExtraBits[index];
            u32 extraBits = 0;

            if(bitCount)
            {
                extraBits = readBits(src, bitCount, bitOffset, iterator);
            }

            length = (u16)(lengthBases[index] + extraBits);
        }
        else // symbol == 285
        {
            length = 258;
        }

        symbol = decodeSymbol(distanceTree, src, bitOffset, iterator);

        DS_ASSERT(symbol < 30, "a symbol of 30 or higher cannot be "
                  "interpreted for the distance tree.");

        u32 distance = 0;

        if(symbol < 4)
        {
            distance = symbol + 1;
        }
        else // symbol < 30
        {
            u08 bitCount  = distanceExtraBits[symbol];
            u32 extraBits = 0;

            if(bitCount)
            {
                extraBits = readBits(src, bitCount, bitOffset, iterator);
            }

            distance = distanceBases[symbol] + extraBits;
        }

        u64 produced = (u64)(dst - og);

        if(distance > produced)
        {
            DS_ERROR("trying to go too far back: %"PRIu32" (max %"PRIu64").", distance,
                     produced);
            return 0;
        }
        else if(produced > cap)
        {
            DS_ERROR("output buffer overflow. wrote %"PRIu64", cap %"PRIu64".",
                     produced, cap);
            return 0;
        }
        else if(length + produced > cap)
        {
            DS_ERROR("output buffer overflow. length + produced %"PRIu64" too long, "
                     "max %"PRIu64".", length + produced, cap);
            return 0;
        }

        // DS_TRACE("LZ77: (length: %"PRIu32", distance: %"PRIu32")", length, distance);

        for(u16 l = 0; l < length; ++l)
        {
            // DS_TRACE("wrote LZ77: 0x%X <- byte #%"PRIu64", source byte #%"PRIu64"",
                     // *(dst - distance), (dst - og), (dst - og - distance));
            *dst    = *(dst - distance);
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;

            if(info->bytesWritten >= cap)
            {
                return DS_END_EARLY;
            }
            ++info->bytesWritten;
        }
    }
}

f_internal u08 *readBlock_nohuff
(
    DeflateInfo *info,
    const u08   *src,
    u08         *dst,
    u08         *bitOffset,
    u64         *iterator,
    u32         *adlerA,
    u32         *adlerB,
    u64         cap
){
    DS_TRACE("reading uncompressed block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    if(*bitOffset)
    {
        readBits(src, (8 - *bitOffset), bitOffset, iterator);
    }

    u16 LEN = readBits(src, 8, bitOffset, iterator);
    LEN += (readBits(src, 8, bitOffset, iterator) << 8);
    u16 NLEN = readBits(src, 8, bitOffset, iterator);
    NLEN += (readBits(src, 8, bitOffset, iterator) << 8);

    u16 COMP = LEN ^ 0xFFFF;

    DS_TRACE("identified LEN: %"PRIu32" bytes", LEN);

    if(!LEN)
    {
        DS_TRACE("skipping 0-length block.");
        return dst;
    }

    if(NLEN != COMP)
    {
        DS_ERROR("NLEN does not match 1's complement of LEN: LEN: %"PRIu32", "
                 "NLEN: %"PRIu32", COMP: %"PRIu32"", LEN, NLEN, COMP);
        return 0;
    }

    for(u32 j = 0; j < LEN; ++j)
    {
        *dst    = src[*iterator];
        *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
        *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
        *iterator += 1;

        if(info->bytesWritten >= cap)
        {
            return DS_END_EARLY;
        }
        ++info->bytesWritten;
    }

    return dst;
}

f_internal u08 *readBlock_static
(
    DeflateInfo *info,
    const u08   *src,
    u08         *dst,
    u08         *og,
    u08         *bitOffset,
    u64         *iterator,
    u32         *adlerA,
    u32         *adlerB,
    u64         cap
){
    DS_TRACE("reading static block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    u16 litLenLengths[288] = {0};

    for(u16 i = 0; i < 144; ++i)
    {
        litLenLengths[i] = 8;
    }
    for(u16 i = 144; i < 256; ++i)
    {
        litLenLengths[i] = 9;
    }
    for(u16 i = 256; i < 280; ++i)
    {
        litLenLengths[i] = 7;
    }
    for(u16 i = 280; i < 288; ++i)
    {
        litLenLengths[i] = 8;
    }
    u16 distLengths[32] = {0};
    for(u16 i = 0; i < 32; ++i)
    {
        distLengths[i] = 5;
    }

    HuffmanTree literalLengthTree = {0};
    HuffmanTree distanceTree      = {0};

    buildTree(&literalLengthTree, litLenLengths, 288);
    buildTree(&distanceTree, distLengths, 32);

    dst = decodeHuffmanTrees(info, &literalLengthTree, &distanceTree, src, dst, og,
                             bitOffset, iterator, adlerA, adlerB, cap);

    destroyTree(&literalLengthTree);
    destroyTree(&distanceTree);

    return dst;
}

f_internal u08 *readBlock_dynamic
(
    DeflateInfo *info,
    const u08   *src,
    u08         *dst,
    u08         *og,
    u08         *bitOffset,
    u64         *iterator,
    u32         *adlerA,
    u32         *adlerB,
    u64         cap
){
    DS_TRACE("reading dynamic block at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    DynHuffBlock dBlock = {0};

    dBlock.HLIT  = (u08)readBits(src, 5, bitOffset, iterator);
    dBlock.HDIST = (u08)readBits(src, 5, bitOffset, iterator);
    dBlock.HCLEN = (u08)readBits(src, 4, bitOffset, iterator);

    DS_TRACE("HLIT:  %2"PRIu16", actual: %3"PRIu16, dBlock.HLIT,  dBlock.HLIT  + 257);
    DS_TRACE("HDIST: %2"PRIu16", actual: %3"PRIu16, dBlock.HDIST, dBlock.HDIST + 1);
    DS_TRACE("HCLEN: %2"PRIu16", actual: %3"PRIu16, dBlock.HCLEN, dBlock.HCLEN + 4);
    DS_TRACE("read header at byte %"PRIu64", bit offset %"PRIu8".",
             *iterator, *bitOffset);

    u16 compressLengths[19] = {0};

    // read HCLEN + 4 number of codelengths, each being 3 bits.
    for(u08 j = 0; j < dBlock.HCLEN + 4; ++j)
    {
        u08 symbol = dynHuffCodelenghOrder[j];
        compressLengths[symbol] = (u08)readBits(src, 3, bitOffset, iterator);
    }

    u16 litLenTreeLength = dBlock.HLIT  + 257;
    u08 distTreeLength   = dBlock.HDIST + 1;
    u16 totalLength      = litLenTreeLength + distTreeLength;
    u16 litDistLengths[totalLength];
    u16 previousLength = 0;

    // I now need to model and construct the "canonical" huffman tree
    // using the lengths in compressLengths, before I can obtain the
    // codes for the other two trees.
    HuffmanTree encodedTree = {0};
    buildTree(&encodedTree, compressLengths, 19);

    for(u16 j = 0; j < totalLength; ++j)
    {
        u16 symbol = decodeSymbol(&encodedTree, src, bitOffset, iterator);

        DS_ASSERT(symbol < 19, "A symbol above 18 (%"PRIu32") from the compressed tree"
                  " cannot be interpreted.", symbol);

        if(symbol < 16)
        {
            // literal length
            litDistLengths[j] = (u08)symbol;
            previousLength    = (u08)symbol;
        }
        else if(symbol == 16)
        {
            // repeat previous length, 3-6 times
            u08 repeat = 3 + (u08)readBits(src, 2, bitOffset, iterator);
            for(u08 k = 0; k < repeat; ++k)
            {
                DS_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
                          " exceeds maximum of %"PRIu32".", j, totalLength)
                litDistLengths[j + k] = previousLength;
            }
            j += repeat - 1;
        }
        else if(symbol == 17)
        {
            // repeat zero, 3-10 times
            u08 repeat = 3 + (u08)readBits(src, 3, bitOffset, iterator);
            for(u08 k = 0; k < repeat; ++k)
            {
                DS_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
                          " exceeds maximum of %"PRIu32".", j, totalLength)
                litDistLengths[j + k] = 0;
            }
            j += repeat - 1;
            previousLength = 0;
        }
        else if(symbol == 18)
        {
            // repeat zero, 11-138 times.
            u08 repeat = 11 + (u08)readBits(src, 7, bitOffset, iterator);
            for(u08 k = 0; k < repeat; ++k)
            {
                DS_ASSERT(j + k < totalLength, "index into litDistLengths %"PRIu32
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

    dst = decodeHuffmanTrees(info, &literalLengthTree, &distanceTree, src, dst, og,
                             bitOffset, iterator, adlerA, adlerB, cap);

    destroyTree(&literalLengthTree);
    destroyTree(&distanceTree);
    destroyTree(&encodedTree);

    return dst;
}

DeflateInfo dsReadDeflate
(
    const u08 *src,
    u08       *dst,
    u32       *checksum,
    u64       cap
){
    u08 *og = dst;

    u08 bitOffset = 0;
    u32 adlerA    = 1;
    u32 adlerB    = 0;

    DeflateInfo  resultInfo = {0};
    DeflateBlock block      = {0};

    u64 i = 0;
    bool endStream = false;
    while(!endStream)
    {
        DS_TRACE("starting new block at byte %"PRIu64", bit offset %"PRIu8".",
                 i, bitOffset);
        block.BFINAL = (u08)readBits(src, 1, &bitOffset, &i);
        if(block.BFINAL)
        {
            DS_TRACE("BFINAL found.");
            resultInfo.doneReading = true;
            endStream = true;
        }

        block.BTYPE = (u08)readBits(src, 2, &bitOffset, &i);
        DS_TRACE("BTYPE: %"PRIu8"", block.BTYPE);

        if(block.BTYPE == BTYPE_UNCROMPRESSED)
        {
            dst = readBlock_nohuff(&resultInfo, src, dst, &bitOffset, &i, &adlerA,
                                   &adlerB, cap);
            if(dst == DS_END_EARLY)
            {
                resultInfo.doneReading = false;
                endStream = true;
                break;
            }
            else if(!dst)
            {
                DS_ERROR("failed to read uncompressed huffman block.");
                goto error;
            }
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            dst = readBlock_static(&resultInfo, src, dst, og, &bitOffset, &i, &adlerA,
                                   &adlerB, cap);
            if(dst == DS_END_EARLY)
            {
                resultInfo.doneReading = false;
                endStream = true;
                break;
            }
            else if(!dst)
            {
                DS_ERROR("failed to read static huffman block.");
                goto error;
            }
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            dst = readBlock_dynamic(&resultInfo, src, dst, og, &bitOffset, &i,
                                    &adlerA, &adlerB, cap);
            if(dst == DS_END_EARLY)
            {
                resultInfo.doneReading = false;
                endStream = true;
                break;
            }
            else if(!dst)
            {
                DS_ERROR("failed to read dynamic huffman block.");
                goto error;
            }
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            DS_ERROR("BTYPE of 3 is reserved.");
            goto error;
        }
        DS_TRACE("ending deflate block at byte index %"PRIu64", bit offset %"PRIu8".",
                 i, bitOffset);
    }

    if(bitOffset)
    {
        bitOffset = 0;
        ++i;
    }

    DS_TRACE("ending deflate stream at byte index %"PRIu64", bit offset %"PRIu8".",
             i, bitOffset);

    *checksum = (adlerB << 16) | adlerA;
    resultInfo.success   = true;
    resultInfo.bytesRead = i;
    return resultInfo;

error:
    return resultInfo;
}
