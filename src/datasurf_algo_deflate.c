#include "datasurf_main.h"
#include "ds_print_macros.h"

#include <inttypes.h>

#include "pd_dyn_arr.h"

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

const u08 bitmasks[9] =
{
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

f_internal u16 readBits
(
    const u08 *src,
    u08       bitCount,
    u08       *bitOffset,
    u64       *iterator
){
    DS_ASSERT(*bitOffset < 8, "bitOffset cannot be bigger than 7.");
    DS_ASSERT(bitCount < 16, "maximum bit count to be read is 16.");

    u16 value    = 0;
    u08 bitsRead = 0;

    while(bitCount > 0)
    {
        u08 available = 8 - *bitOffset;
        u08 take      = bitCount < available ? bitCount : available;
        u16 part      = (src[*iterator] >> *bitOffset) & bitmasks[take];

        value      |= part << bitsRead;
        bitsRead   += take;
        bitCount   -= take;
        *bitOffset += take;

        if(*bitOffset == 8)
        {
            *bitOffset = 0;
            ++(*iterator);
        }
    }

    return value;
}

f_internal u16 reverseBits
(
    u16 code,
    u16 length
){
    DS_ASSERT(length < 16, "cannot reverse more than 15 bits.");

    u16 result = 0;

    for(u08 i = 0; i < length; ++i)
    {
        result = (u16)((result << 1) | (code & 1));
        code >>= 1;
    }

    DS_ASSERT(result < (1u << length), "result (length %"PRIu32") %"PRIu32" >= %"
              PRIu32" (maximum)", length, result, 1u << length);

    return result;
}

f_internal void makeCanonicalCodes
(
    const u16   *lengths,
    u16         symbolCount,
    HuffmanCode *codes
){
    u16 count[MAX_CODELEN + 1]    = {0};
    u16 nextCode[MAX_CODELEN + 1] = {0};

    for(u16 symbol = 0; symbol < symbolCount; ++symbol)
    {
        if(lengths[symbol])
        {
            ++count[lengths[symbol]];
        }
    }

    u16 code = 0;
    for(u08 bits = 1; bits < MAX_CODELEN + 1; ++bits)
    {
        code = (u16)((code + count[bits - 1]) << 1);
        nextCode[bits] = code;
    }

    i32 left = 1;
    for(u08 i = 1; i < MAX_CODELEN + 1; ++i)
    {
        left = (left << 1u) - count[i];
        DS_ASSERT(left > -1, "tree is oversubscribed.")
    }

    for(u16 symbol = 0; symbol < symbolCount; ++symbol)
    {
        u16 length = lengths[symbol];

        codes[symbol].symbol = symbol;
        codes[symbol].length = length;
        codes[symbol].code   = 0;

        if(length)
        {
            u16 canonical = nextCode[length]++;

            DS_ASSERT(canonical < (1u << length), "canonical code overflow: "
                      "symbol: %"PRIu32" length: %"PRIu32" code: %"PRIu32"", symbol,
                      length, canonical);

            codes[symbol].code = reverseBits(canonical, length);
        }
    }
}

f_internal void insertCode
(
    HuffmanTree *tree,
    u16         code,
    u16         symbol,
    u16         length
){
    i32 node = 0;

    for(u08 i = 0; i < length; ++i)
    {
        u08 bit   = (code >> i) & 1;
        i32 child = tree->nodes[node].children[bit];

        if(child < 0)
        {
            HuffmanNode tmp = {0};
            tmp.symbol      = -1;
            tmp.children[0] = -1;
            tmp.children[1] = -1;

            pdArrPush(tree->nodes, tmp);
            child = (i16)(pdArrSize(tree->nodes) - 1);
            tree->nodes[node].children[bit] = child;

            DS_ASSERT(child >= 0 && child < (i16)pdArrSize(tree->nodes),
                      "invalid Huffman child index: %"PRIi16", node count: %"PRIu64,
                      child, pdArrSize(tree->nodes));
        }

        node = child;
    }

    DS_ASSERT(tree->nodes[node].symbol == -1, "huffman tree is constructed incorrectly."
              "\nSymbol %"PRIu32" at node %"PRIu32" cannot be overwritten.",
              tree->nodes[node].symbol, node);

    tree->nodes[node].symbol = (i16)symbol;
}

f_internal void buildTree
(
    HuffmanTree *tree,
    const u16   *lengths,
    u16         symbolCount
){
    HuffmanNode node   = {0};
    HuffmanCode *codes = calloc(symbolCount, sizeof(HuffmanCode));

    DS_ASSERT(codes, "failed to allocate HuffmanCode array.")

    // root node
    node.symbol      = -1;
    node.children[0] = -1;
    node.children[1] = -1;
    pdArrPush(tree->nodes, node);

    makeCanonicalCodes(lengths, symbolCount, codes);

    for(u16 i = 0; i < symbolCount; ++i)
    {
        if(!codes[i].length)
        {
            continue;
        }
        insertCode(tree, codes[i].code, codes[i].symbol, codes[i].length);
    }

    DS_TRACE("inserted %"PRIu32" codes into the tree.", symbolCount);

    free(codes);
}

f_internal void destroyTree
(
    HuffmanTree *tree
){
    if(tree->nodes)
    {
        pdArrFree(tree->nodes);
    }
}

// PERF: bottleneck.
f_internal u16 decodeSymbol
(
    const HuffmanTree *tree,
    const u08         *src,
    u08               *currBitOffset,
    u64               *iterator
){
    DS_ASSERT(*currBitOffset < 8, "currBitOffset cannot be bigger than 7.");

    i32 node = 0;

    // read bits until the constructed code matches a value in the huffman tree that's
    // used to read the other two huffman trees.
    for(u08 i = 0; i < MAX_CODELEN; ++i)
    {
        u08 bit = (u08)readBits(src, 1, currBitOffset, iterator);
        node = tree->nodes[node].children[bit];

        if(node < 0)
        {
            DS_ERROR("huffman code is invalid for the tree that was given.");
            return U16MAX;
        }

        if(tree->nodes[node].symbol >= 0)
        {
            return (u16)tree->nodes[node].symbol;
        }
    }

    DS_ERROR("could not find huffman code (%"PRIu32") in the tree that was given "
             "within %"PRIu32" bits.", node, MAX_CODELEN);
    return U16MAX;
}

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

f_internal u08 *readNoHuffBlock
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

f_internal u08 *readStaticBlock
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

f_internal u08 *readDynamicBlock
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
            dst = readNoHuffBlock(&resultInfo, src, dst, &bitOffset, &i, &adlerA,
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
            dst = readStaticBlock(&resultInfo, src, dst, og, &bitOffset, &i, &adlerA,
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
            dst = readDynamicBlock(&resultInfo, src, dst, og, &bitOffset, &i,
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

    DS_TRACE("ending deflate stream at byte index %"PRIu64", bit offset %"PRIu8".", i,
             bitOffset);

    *checksum = (adlerB << 16) | adlerA;
    resultInfo.success   = true;
    resultInfo.bytesRead = i;
    return resultInfo;

error:
    return resultInfo;
}
