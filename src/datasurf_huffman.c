#include "datasurf_main.h"
#include "datasurf_huffman.h"

#include "pd_print_macros.h"
#include "dynamic_array.h"

const uint8_t bitmasks[9] =
{
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

uint16_t readBits
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

uint16_t reverseBits
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

void makeCanonicalCodes
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

void insertCode
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

void buildTree
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

void destroyTree
(
    HuffmanTree *tree
){
    if(tree->nodes)
    {
        pdArrFree(tree->nodes);
    }
}

uint16_t decodeSymbol
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
