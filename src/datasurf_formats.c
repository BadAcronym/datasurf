#include "datasurf_main.h"

#include <stdio.h>

typedef struct ZLibInfo
{
    uint8_t CM     : 4;
    uint8_t CINFO  : 4;
    uint8_t FCHECK : 5;
    uint8_t FDICT  : 1;
    uint8_t FLEVEL : 2;
}
ZlibInfo;

bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest,
    uint64_t maxDeflateLen
){
    uint8_t CMF    = zlib[0];
    uint8_t FLG    = zlib[1];
    uint8_t DICTID = zlib[2];

    ZlibInfo info = {0};
    info.CM     = CMF & 15;
    info.CINFO  = (CMF >> 4);

    info.FCHECK = FLG & 31;
    info.FDICT  = (FLG >> 5) & 1;
    info.FLEVEL = (FLG >> 6);

    if(info.CM != 8)
    {
        fprintf(stderr, "\033[31;1mcould not validate CMF in zlib data. "
                "expected: 8, got: %u.\033[0m", info.CM);
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
    fprintf(stderr, "CM:     %u\n", info.CM);
    fprintf(stderr, "CINFO:  %u\n", info.CINFO);
    fprintf(stderr, "FCHECK: %u\n", info.FCHECK);
    fprintf(stderr, "FDICT:  %u\n", info.FDICT);
    fprintf(stderr, "FLEVEL: %u\n", info.FLEVEL);
    fprintf(stderr, "DICTID: %u\n", DICTID);
    #endif

    uint32_t madeChecksum = 0;

    uint64_t bytesRead = dsReadDeflate(zlib, dest, info.CINFO, info.FCHECK, info.FDICT,
                                       &madeChecksum);

    uint32_t readChecksum = *(uint32_t*)(&zlib[3 + bytesRead]);

    #ifdef DEBUG
    fprintf(stderr, "made checksum: %u\n", madeChecksum);
    fprintf(stderr, "read checksum: %u\n", readChecksum);
    #endif

    return madeChecksum == readChecksum;
}
