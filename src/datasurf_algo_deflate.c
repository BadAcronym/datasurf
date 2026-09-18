#include "datasurf_main.h"
#include "datasurf_huffman.h"

#include "pd_print_macros.h"

const uint8_t dynHuffCodelenghOrder[19] =
{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
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
    PD_DEBUG("decoding max. %lu symbols from huffman tree.", cap);

    for(uint64_t j = 0; j < cap; ++j)
    {
        uint16_t symbol = decodeSymbol(litLenTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 286, "a symbol of 286 or higher cannot be "
                  "interpreted for the literal/length tree.");

        if(symbol < 256)
        {
            PD_TRACE("wrote literal: 0x%X", symbol);
            *dst    = (uint8_t)symbol;
            *adlerA = (*adlerA + *dst++)  % ADLER_PRIME;
            *adlerB = (*adlerB + *adlerA) % ADLER_PRIME;
            continue;
        }
        else if(symbol == 256)
        {
            PD_DEBUG("ending dynamic huffman block.");
            return dst;
        }

        int step = 0, a = 0, b = 0, c = 0;
        uint32_t extraBits = 0;

        uint16_t length = 0;

        if(symbol < 285){
        	step = symbol < 265 ? 0 : ((symbol - 264 + 3) >> 2);

			a = (4 << step) + 3;
			b = (1 << step);
			c = (step * 4) + 261;

			//Behaviour observation:
			//if the readBits function is left outside of this conditional
			//it produces the correct outcome, but still throws errors
			extraBits = readBits(src, step, bitOffset, iterator);
			length = a + extraBits + b * (symbol - c);
        }
        else{
        	length = 258;
        }

        symbol = decodeSymbol(distanceTree, src, bitOffset, iterator);

        PD_ASSERT(symbol < 30, "a symbol of 30 or higher cannot be "
                  "interpreted for the distance tree.");

        step = symbol < 4 ? 0 : ((symbol - 2) >> 1);

        a = (2 << step) + 1;
        b = (1 << step);
        c = (step * 2) + 2;

        extraBits = readBits(src, step, bitOffset, iterator);
        uint32_t distance = a + extraBits + b * (symbol - c);

        uint64_t produced = (uint64_t)(dst - og);

        if(distance > produced)
        {
            PD_ERROR("trying to go too far back: %u (max %lu).", distance, produced);
            return 0;
        }
        else if(produced > cap || length > cap - produced)
        {
            PD_ERROR("output buffer overflow. length %u too long, max %lu.",
                     length, cap - produced);
            return 0;
        }

        PD_TRACE("LZ77: (length: %u, distance: %u)", length, distance);

        for(uint16_t l = 0; l < length; ++l)
        {
            PD_TRACE("wrote LZ77: 0x%X <- byte #%lu, source byte #%lu",
                     *(dst - distance), (dst - og), (dst - og - distance));
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
    // skip to next byte
    *bitOffset = 0;
    *iterator += 1;
    uint16_t LEN  = src[*iterator] + (uint16_t)(src[*iterator + 1] << 8);
    *iterator += 2;
    uint16_t NLEN = src[*iterator] + (uint16_t)(src[*iterator + 1] << 8);
    *iterator += 2;
    uint16_t COMP = LEN ^ 65535;

    PD_DEBUG("identified LEN: %u bytes", LEN);

    PD_ASSERT(LEN - 1 < cap - (uint64_t)(dst - og), "output buffer overflow. trying to "
              "read length %u, max %lu.", LEN, cap - (uint64_t)(dst - og));

    if(LEN > cap - (uint64_t)(dst - og))
    {
        return 0;
    }

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
    uint8_t       *og,
    uint8_t       *bitOffset,
    uint64_t      *iterator,
    uint32_t      *adlerA,
    uint32_t      *adlerB,
    uint64_t      cap
){
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

    bool endStream = false;

    DeflateInfo  resultInfo = {0};
    DeflateBlock block      = {0};

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
            uint8_t *start = dst;
            dst = readBlock_nohuff(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            uint8_t *start = dst;
            dst = readBlock_static(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            uint8_t *start = dst;
            dst = readBlock_dynamic(src, dst, og, &bitOffset, &i, &adlerA, &adlerB, cap);
            resultInfo.bytesWritten += (uint64_t)(dst - start);
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            goto result;
        }

        if(!dst)
        {
            PD_ERROR("dst ptr was set to null. internal error.");
            goto result;
        }
    }

    *checksum = (adlerB << 16) | adlerA;

result:
    if(bitOffset)
    {
        ++i;
    }
    resultInfo.compressedBytesRead = i - 1;
    return resultInfo;
}
