#include "datasurf_main.h"
#include "pd_print_macros.h"

typedef struct ZLibInfo
{
    uint8_t CM     : 4;
    uint8_t CINFO  : 4;
    uint8_t FCHECK : 5;
    uint8_t FDICT  : 1;
    uint8_t FLEVEL : 2;
}
ZlibInfo;

typedef union ZLibUnion
{
    uint8_t  data[3];
    ZlibInfo info;
    struct
    {
        uint8_t CMF;
        uint8_t FLG;
        uint8_t DICTID;
    }
    og;
}
ZLibUnion;

bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest
){
    ZLibUnion uInfo = { .data = {zlib[0], zlib[1], zlib[2]} };
    ZlibInfo  info  = uInfo.info;

    if(info.CM != 8)
    {
        PD_ERROR("could not validate CMF in zlib data. "
                "expected: 8, got: %u.", info.CM);
        return false;
    }

    if((uInfo.og.CMF * 256 + uInfo.og.FLG) % 31 != 0)
    {
        PD_ERROR("Failed zlib header integrity check: CMF*256 + FLG "
                "is not a multiple of 31, but instead: %u.",
                uInfo.og.CMF * 256 + uInfo.og.FLG);
        return false;
    }

    PD_DEBUG("CM:     %u", info.CM);
    PD_DEBUG("CINFO:  %u", info.CINFO);
    PD_DEBUG("FCHECK: %u", info.FCHECK);
    PD_DEBUG("FDICT:  %u", info.FDICT);
    PD_DEBUG("FLEVEL: %u", info.FLEVEL);
    PD_DEBUG("DICTID: %u", uInfo.og.DICTID);

    uint32_t madeChecksum = 0;

    uint64_t bytesRead = dsReadDeflate(zlib + 3, dest, info.CINFO, info.FCHECK,
                                       info.FDICT, &madeChecksum);

    if(!bytesRead)
    {
        PD_ERROR("couldn't read any data from provided deflate stream.");
    }

    uint32_t readChecksum = *(uint32_t*)(&zlib[3 + bytesRead]);

    PD_DEBUG("made checksum: %u", madeChecksum);
    PD_DEBUG("read checksum: %u", readChecksum);

    return madeChecksum == readChecksum;
}
