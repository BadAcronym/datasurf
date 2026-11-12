#include "datasurf_main.h"
#include "ds_print_macros.h"
#include "datasurf_huffman.h"

#include "pd_dyn_arr.h"

#include <inttypes.h>

const u08 bitmasks[9] =
{
    0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
};

u16 readBits
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

u16 reverseBits
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

void makeCanonicalCodes
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

void insertCode
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

void buildTree
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

void destroyTree
(
    HuffmanTree *tree
){
    if(tree->nodes)
    {
        pdArrFree(tree->nodes);
    }
}

u16 decodeSymbol
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
