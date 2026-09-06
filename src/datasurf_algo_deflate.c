#include "datasurf_main.h"

#include <stdio.h>

uint64_t dsReadDeflate
(
    uint8_t  *src,
    uint8_t  *dst,
    uint8_t  CINFO,
    uint8_t  FCHECK,
    uint8_t  FDICT,
    uint32_t *checksum
){
    bool endStream = false;

    uint64_t i = 0;
    for(; !endStream; ++i)
    {
        if(src[i] & 1)
        {
            #ifdef DEBUG
            fprintf(stderr, "BFINAL at byte %u\n", i);
            #endif
            break;
        }

        // 3 bit header

    }

    //
    fprintf(stderr, "\033[31;3mERROR: deflate not implemented yet.\033[0m\n");
    return false;
    //

    return i;
}
