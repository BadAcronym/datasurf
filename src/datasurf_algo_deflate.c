#include "datasurf_main.h"
#include "pd_print_macros.h"
#include "dynamic_array.h"

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
    uint16_t length;
    uint16_t symbol;
}
HuffmanCode;

typedef struct HuffmanNode
{
    int16_t symbol;
    int16_t children[2];
}
HuffmanNode;

typedef struct HuffmanTree
{
    HuffmanNode *nodes;
    uint16_t    nodeCount;
}
HuffmanTree;

const uint8_t dynHuffCodelenghOrder[19] =
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
    uint8_t       *bitOffset,
    uint64_t      *iterator
){
    PD_ASSERT(*bitOffset < 8, "bitOffset cannot be bigger than 7.");
    PD_ASSERT(bitCount < 16, "maximum bit count to be read is 16.");

    uint16_t value    = 0;
    uint8_t  bitsRead = 0;

    while(bitCount > 0)
    {
        uint8_t  available = 8 - *bitOffset;
        uint8_t  take      = bitCount < available ? bitCount : available;
        uint16_t part      = (src[*iterator] >> *bitOffset) & bitmasks[take];

        value      |= part << bitsRead;
        bitsRead   += take;
        bitCount   -= take;
        *bitOffset += take;

        if(*bitOffset > 7)
        {
            *bitOffset = 0;
            ++(*iterator);
        }
    }

    return value;
}

f_internal uint16_t reverseBits
(
    uint16_t code,
    uint16_t length
){
    PD_ASSERT(length < 16, "cannot reverse more than 15 bits.");

    uint16_t result = 0;

    for(uint8_t i = 0; i < length; ++i)
    {
        result = (uint16_t)((result << 1) | (code & 1));
        code >>= 1;
    }

    PD_ASSERT(result < (1 << length), "result (length %u) %u >= %u (maximum)",
              length, 1 << length, result);

    return result;
}

f_internal void makeCanonicalCodes
(
    const uint16_t *lengths,
    uint16_t       symbolCount,
    HuffmanCode    *codes
){
    uint16_t count[MAX_CODELEN + 1]    = {0};
    uint16_t nextCode[MAX_CODELEN + 1] = {0};

    for(uint16_t symbol = 0; symbol < symbolCount; ++symbol)
    {
        if(lengths[symbol])
        {
            ++count[lengths[symbol]];
        }
    }

    uint16_t code = 0;
    for(uint8_t bits = 1; bits < MAX_CODELEN + 1; ++bits)
    {
        code = (uint16_t)((code + count[bits - 1]) << 1);
        nextCode[bits] = code;
    }

    for(uint16_t symbol = 0; symbol < symbolCount; ++symbol)
    {
        uint16_t length = lengths[symbol];

        codes[symbol].symbol = symbol;
        codes[symbol].length = length;
        codes[symbol].code   = 0;

        if(length)
        {
            uint16_t canonical = nextCode[length]++;
            codes[symbol].code = reverseBits(canonical, length);
        }
    }
}

f_internal void insertCode
(
    HuffmanTree *tree,
    uint16_t    code,
    uint16_t    symbol,
    uint16_t    length
){
    int32_t node = 0;

    for(uint8_t i = 0; i < length; ++i)
    {
        uint8_t bit   = (code >> i) & 1;
        int16_t child = tree->nodes[node].children[bit];

        if(child < 0)
        {
            HuffmanNode tmp = {0};
            tmp.symbol      = -1;
            tmp.children[0] = -1;
            tmp.children[1] = -1;

            pdArrPush(tree->nodes, tmp);
            child = (int16_t)(pdArrSize(tree->nodes) - 1);
            tree->nodes[node].children[bit] = child;

            PD_ASSERT(child >= 0 && child < (int16_t)pdArrSize(tree->nodes),
                      "invalid Huffman child index: %d, node count: %zu",
                      child, pdArrSize(tree->nodes));
        }

        node = child;
    }

    PD_ASSERT(tree->nodes[node].symbol == -1, "huffman tree is constructed incorrectly."
              "\nSymbol %u at node %u cannot be overwritten.",
              tree->nodes[node].symbol, node);

    tree->nodes[node].symbol = (int16_t)symbol;
}

f_internal void buildTree
(
    HuffmanTree    *tree,
    const uint16_t *lengths,
    uint16_t       symbolCount
){
    HuffmanNode node   = {0};
    HuffmanCode *codes = calloc(symbolCount, sizeof(HuffmanCode));

    PD_ASSERT(codes, "failed to allocate HuffmanCode array.")

    // root node
    node.symbol      = -1;
    node.children[0] = -1;
    node.children[1] = -1;
    pdArrPush(tree->nodes, node);

    makeCanonicalCodes(lengths, symbolCount, codes);

    for(uint16_t i = 0; i < symbolCount; ++i)
    {
        if(!codes[i].length)
        {
            continue;
        }
        insertCode(tree, codes[i].code, codes[i].symbol, codes[i].length);
    }

    free(codes);
}

f_internal uint16_t decodeSymbol
(
    const HuffmanTree *tree,
    const uint8_t     *src,
    uint8_t           *currBitOffset,
    uint64_t          *iterator
){
    PD_ASSERT(*currBitOffset < 8, "currBitOffset cannot be bigger than 7.");

    int16_t node = 0;

    // read bits until the constructed code matches a value in the huffman tree that's
    // used to read the other two huffman trees.
    for(uint8_t i = 0; i < MAX_CODELEN; ++i)
    {
        uint8_t bit = (uint8_t)readBits(src, 1, currBitOffset, iterator);
        node = tree->nodes[node].children[bit];

        if(node < 0)
        {
            PD_ERROR("huffman code is invalid for the tree that was given.");
            return UINT16_MAX;
        }

        if(tree->nodes[node].symbol >= 0)
        {
            return (uint16_t)tree->nodes[node].symbol;
        }
    }

    PD_ERROR("could not find huffman code (%u) in the tree that was given within %u "
             "bits.", node, MAX_CODELEN);
    return UINT16_MAX;
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
    uint8_t  *og = dst;

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
                *dst   = *src++;
                adlerA = (adlerA + *dst)   % ADLER_PRIME;
                adlerB = (adlerB + adlerA) % ADLER_PRIME;
                ++dst;
            }
        }
        else if(block.BTYPE == BTYPE_STATIC_HUFFMAN)
        {
            PD_ERROR("static huffman block not implemented.");
            return 0;
        }
        else if(block.BTYPE == BTYPE_DYNAMIC_HUFFMAN)
        {
            DynHuffBlock dBlock = {0};

            dBlock.HLIT  = (uint8_t)readBits(src, 5, &bitOffset, &i);
            dBlock.HDIST = (uint8_t)readBits(src, 5, &bitOffset, &i);
            dBlock.HCLEN = (uint8_t)readBits(src, 4, &bitOffset, &i);

            PD_DEBUG("HLIT:  %2u, actual: %3u", dBlock.HLIT,  dBlock.HLIT  + 257);
            PD_DEBUG("HDIST: %2u, actual: %3u", dBlock.HDIST, dBlock.HDIST + 1);
            PD_DEBUG("HCLEN: %2u, actual: %3u", dBlock.HCLEN, dBlock.HCLEN + 4);

            uint16_t compressLengths[19] = {0};

            // read HCLEN + 4 number of codelengths, each being 3 bits.
            for(uint8_t j = 0; j < dBlock.HCLEN + 4; ++j)
            {
                uint8_t symbol = dynHuffCodelenghOrder[j];
                compressLengths[symbol] = (uint8_t)readBits(src, 3, &bitOffset, &i);
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
                uint16_t symbol = decodeSymbol(&encodedTree, src, &bitOffset, &i);

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
                    uint8_t repeat = 3 + (uint8_t)readBits(src, 2, &bitOffset, &i);
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
                    uint8_t repeat = 3 + (uint8_t)readBits(src, 3, &bitOffset, &i);
                    for(uint8_t k = 0; k < repeat; ++k)
                    {
                        litDistLengths[j + k] = 0;
                        PD_ASSERT(j + k < totalLength, "index into litDistLengths "
                                  "%u exceeds maximum of %u.", j, totalLength)
                    }
                    j += repeat - 1;
                }
                else if(symbol == 18)
                {
                    // repeat zero, 11-138 times.
                    uint8_t repeat = 11 + (uint8_t)readBits(src, 7, &bitOffset, &i);
                    for(uint8_t k = 0; k < repeat; ++k)
                    {
                        litDistLengths[j + k] = 0;
                        PD_ASSERT(j + k < totalLength, "index into litDistLengths "
                                  "%u exceeds maximum of %u.", j, totalLength)
                    }
                    j += repeat - 1;
                }
            }

            HuffmanTree literalLengthTree = {0};
            HuffmanTree distanceTree      = {0};
            buildTree(&literalLengthTree, litDistLengths, litLenTreeLength);
            buildTree(&distanceTree, &litDistLengths[litLenTreeLength],
                      distTreeLength);

            bool endBlock = false;

            // with the two trees constructed, we can go through them and separate the
            // data into these arrays. we cannot otherwise decode in one go, because it
            // requires decoding both trees first, which are variable bitlength symbols.

            uint8_t  literals[litLenTreeLength];
            uint16_t marks[litLenTreeLength];
            uint16_t lengths[litLenTreeLength];
            uint32_t distances[litLenTreeLength];
            uint16_t litIndex  = 0;
            uint16_t markIndex = 0;
            uint16_t lenIndex  = 0;
            uint16_t distIndex = 0;

            // now that we have the other two trees constructed, we can use those to
            // decode the actual data. yes?
            for(uint16_t j = 0; j < litLenTreeLength; ++j)
            {
                uint16_t symbol = decodeSymbol(&literalLengthTree, src, &bitOffset, &i);

                PD_ASSERT(symbol < 286, "a symbol of 286 or higher cannot be "
                          "interpreted for the literal/length tree.");

                if(symbol < 256)
                {
                    literals[litIndex++] = (uint8_t)symbol;
                }
                else if(symbol == 256)
                {
                    endBlock = true;
                    break;
                }
                else if(symbol < 265)
                {
                    uint16_t length     = symbol - 254;
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else if(symbol < 269)
                {
                    uint8_t  extraBits  = (uint8_t)readBits(src, 1, &bitOffset, &i);
                    uint16_t length     = 11 + extraBits + 2 * (symbol - 265);
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else if(symbol < 273)
                {
                    uint8_t  extraBits  = (uint8_t)readBits(src, 2, &bitOffset, &i);
                    uint16_t length     = 19 + extraBits + 4 * (symbol - 269);
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else if(symbol < 277)
                {
                    uint8_t extraBits   = (uint8_t)readBits(src, 3, &bitOffset, &i);
                    uint16_t length     = 35 + extraBits + 8 * (symbol - 273);
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else if(symbol < 281)
                {
                    uint8_t extraBits   = (uint8_t)readBits(src, 4, &bitOffset, &i);
                    uint16_t length     = 67 + extraBits + 16 * (symbol - 277);
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else if(symbol < 285)
                {
                    uint8_t extraBits   = (uint8_t)readBits(src, 5, &bitOffset, &i);
                    uint16_t length     = 131 + extraBits + 32 * (symbol - 281);
                    lengths[lenIndex++] = length;
                    marks[markIndex++]  = litIndex;
                }
                else // symbol == 285
                {
                    lengths[lenIndex++] = 258;
                    marks[markIndex++]  = litIndex;
                }
            }

            for(uint16_t j = 0; !endBlock && j < distTreeLength; ++j)
            {
                uint16_t symbol = decodeSymbol(&distanceTree, src, &bitOffset, &i);

                PD_ASSERT(symbol < 30, "a symbol of 30 or higher cannot be "
                          "interpreted for the literal/length tree.");

                if(symbol < 4)
                {
                    distances[distIndex++] = symbol + 1;
                }
                else if(symbol < 6)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 1, &bitOffset, &i);
                    uint32_t distance      = 5 + extraBits + 2 * (symbol - 4);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 8)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 2, &bitOffset, &i);
                    uint32_t distance      = 9 + extraBits + 4 * (symbol - 6);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 10)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 3, &bitOffset, &i);
                    uint32_t distance      = 17 + extraBits + 8 * (symbol - 8);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 12)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 4, &bitOffset, &i);
                    uint32_t distance      = 33 + extraBits + 16 * (symbol - 10);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 14)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 5, &bitOffset, &i);
                    uint32_t distance      = 65 + extraBits + 32 * (symbol - 12);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 16)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 6, &bitOffset, &i);
                    uint32_t distance      = 129 + extraBits + 64 * (symbol - 14);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 18)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 7, &bitOffset, &i);
                    uint32_t distance      = 257 + extraBits + 128 * (symbol - 16);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 20)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 8, &bitOffset, &i);
                    uint32_t distance      = 513 + extraBits + 256 * (symbol - 18);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 22)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 9, &bitOffset, &i);
                    uint32_t distance      = 1025 + extraBits + 512 * (symbol - 20);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 24)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 10, &bitOffset, &i);
                    uint32_t distance      = 2049 + extraBits + 1024 * (symbol - 22);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 26)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 11, &bitOffset, &i);
                    uint32_t distance      = 4097 + extraBits + 2048 * (symbol - 24);
                    distances[distIndex++] = distance;
                }
                else if(symbol < 28)
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 12, &bitOffset, &i);
                    uint32_t distance      = 8193 + extraBits + 4096 * (symbol - 26);
                    distances[distIndex++] = distance;
                }
                else // symbol < 30
                {
                    uint8_t  extraBits     = (uint8_t)readBits(src, 13, &bitOffset, &i);
                    uint32_t distance      = 16385 + extraBits + 8192 * (symbol - 28);
                    distances[distIndex++] = distance;
                }
            }

            markIndex = 0;
            distIndex = 0;
            lenIndex  = 0;

            // now, believe it or not, we can decode the actual data.
            for(uint16_t j = 0; j < litLenTreeLength; ++j)
            {
                if(j == marks[markIndex])
                {
                    uint32_t distance = distances[distIndex++];
                    uint16_t length   = lengths[lenIndex++];

                    // FIXME: I think i need to use distance not into the literals, but
                    // into the actual decoded buffer and copy from there.

                    for(uint16_t l = 0; l < length; ++l)
                    {
                        PD_ASSERT(j - distance + l < litLenTreeLength,
                                  "trying to access beyond literal buffer end (%u): %u",
                                  litLenTreeLength, j - distance + l);

                        PD_ASSERT(j - distance + l > 0,
                                  "trying to go too far back: %u bytes.",
                                  j - distance + l);
                        *dst   = literals[j - distance + l];
                        adlerA = (adlerA + *dst)   % ADLER_PRIME;
                        adlerB = (adlerB + adlerA) % ADLER_PRIME;
                        ++dst;
                    }

                    ++markIndex;
                }

                *dst   = literals[j];
                adlerA = (adlerA + *dst)   % ADLER_PRIME;
                adlerB = (adlerB + adlerA) % ADLER_PRIME;
                ++dst;
            }
        }
        else // block.BTYPE == BTYPE_RESERVED
        {
            PD_ERROR("BTYPE of 3 is reserved.");
            return 0;
        }
    }

    *checksum = (adlerB << 16) | adlerA;
    PD_DEBUG("read a total of %lu bytes.", dst - og);
    return (uint64_t)(dst - og);
}
