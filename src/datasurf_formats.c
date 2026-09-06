#include "datasurf_main.h"

#include <stdio.h>

bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest,
    uint64_t maxDeflateLen
){
    uint8_t CMF    = zlib[0];
    uint8_t FLG    = zlib[1];
    uint8_t DICTID = zlib[2];

    uint8_t CM     = CMF & 15;
    uint8_t CINFO  = (CMF >> 4);

    uint8_t FCHECK = FLG & 31;
    uint8_t FDICT  = (FLG >> 5) & 1;
    uint8_t FLEVEL = (FLG >> 6);

    if(CM != 8)
    {
        fprintf(stderr, "\033[31;1mcould not validate CMF in zlib data. "
                "expected: 8, got: %u.\033[0m", CM);
        return false;
    }

    if((CMF * 256 + FLG) % 31 != 0)
    {
        fprintf(stderr, "\033[31;1mFailed zlib header integrity check: CMF*256 + FLG "
                "is not a multiple of 31, but instead: %u.\033[0m",
                CMF * 256 + FLG);
        return false;
    }

    #ifdef DEBUG
    fprintf(stderr, "CM: %u\n",     CM);
    fprintf(stderr, "CINFO: %u\n",  CINFO);
    fprintf(stderr, "FCHECK: %u\n", FCHECK);
    fprintf(stderr, "FDICT: %u\n",  FDICT);
    fprintf(stderr, "FLEVEL: %u\n", FLEVEL);
    fprintf(stderr, "DICTID: %u\n", DICTID);
    #endif

    uint32_t madeChecksum = 0;

    uint64_t bytesRead = dsReadDeflate(zlib, dest, CINFO, FCHECK, FDICT, &madeChecksum);

    uint32_t readChecksum = *(uint32_t*)(&zlib[3 + bytesRead]);

    #ifdef DEBUG
    fprintf(stderr, "made checksum: %u\n", madeChecksum);
    fprintf(stderr, "read checksum: %u\n", readChecksum);
    #endif

    return madeChecksum == readChecksum;
}
