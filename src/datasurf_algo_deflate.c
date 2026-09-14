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
    uint8_t  length;
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

        value          |= part << bitsRead;
        bitsRead       += take;
        bitCount       -= take;
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
        uint8_t length = lengths[symbol];

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

f_internal void printTree
(
    const HuffmanTree *tree
){
    uint16_t treeSize = (uint16_t)pdArrSize(tree->nodes);

    for(uint16_t i = 0; i < treeSize; ++i)
    {
        PD_DEBUG("node %u: symbol: %i, children: %i and %i", i, tree->nodes[i].symbol,
                 tree->nodes[i].children[0], tree->nodes[i].children[1]);
    }
}

f_internal void insertCode
(
    HuffmanTree *tree,
    uint16_t    code,
    uint16_t    symbol,
    uint8_t     length
){
    PD_DEBUG("inserting into tree: code: %u, symbol: %u, length: %u",
             code, symbol, length);

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
        PD_DEBUG("read singular bit: %u", bit);
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
            DynHuffBlock dBlock        = {0};

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
                PD_DEBUG("read code length %u for symbol %u.",
                         compressLengths[symbol], symbol);
            }

            uint16_t distanceLengthOffset = dBlock.HDIST + 1;
            uint16_t totalLength = dBlock.HLIT + dBlock.HDIST + 258;
            uint16_t litDistLengths[totalLength];
            uint16_t previousLength = 0;

            // I now need to model and construct the "canonical" huffman tree
            // using the lengths in compressLengths, before I can obtain the
            // codes for the other two trees.
            HuffmanTree encodedTree = {0};
            buildTree(&encodedTree, compressLengths, 19);

            printTree(&encodedTree);

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

                    PD_DEBUG("read literal length of %u from compressed tree.", symbol);
                }
                else if(symbol == 16)
                {
                    // repeat previous length, 3-6 times
                    uint8_t repeat = 3 + (uint8_t)readBits(src, 2, &bitOffset, &i);

                    PD_DEBUG("read repeat previous length (%u) %u times.",
                             previousLength, repeat);
                }
                else if(symbol == 17)
                {
                    // repeat zero, 3-10 times
                    uint8_t repeat = 3 + (uint8_t)readBits(src, 3, &bitOffset, &i);
                    for(uint8_t k = 0; k < repeat; ++k)
                    {
                        litDistLengths[j++] = 0;
                    }

                    PD_DEBUG("read repeat 0 %u times.", repeat);
                }
                else if(symbol == 18)
                {
                    // repeat zero, 11-138 times.
                    uint8_t repeat = 11 + (uint8_t)readBits(src, 7, &bitOffset, &i);
                    for(uint8_t k = 0; k < repeat; ++k)
                    {
                        litDistLengths[j++] = 0;
                    }

                    PD_DEBUG("read repeat 0 %u times.", repeat);
                }
            }

            HuffmanTree literalLengthTree = {0};
            HuffmanTree distanceTree      = {0};
            buildTree(&literalLengthTree, litDistLengths, distanceLengthOffset);
            buildTree(&distanceTree, litDistLengths + distanceLengthOffset,
                      totalLength - distanceLengthOffset);

            // now that we have the other two trees constructed, we can use those to
            // decode the actual data. yes?
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
