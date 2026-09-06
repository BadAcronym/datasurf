#include "datasurf_main.h"
#include "datasurf_info_macros.h"

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
    uint8_t  *dest,
    uint64_t maxDeflateLen
){
    ZLibUnion uInfo = { .data = {zlib[0], zlib[1], zlib[2]} };
    ZlibInfo  info  = uInfo.info;
    // uint8_t CMF     = zlib[0];//can be replaced by uinfo.orig.CMF
    // uint8_t FLG     = zlib[1];//can be replaced by uinfo.orig.FLG
    // uint8_t DICTID  = zlib[2];//can be replaced by uinfo.orig.DICTID

    if(info.CM != 8)
    {
        DS_ERROR("could not validate CMF in zlib data. "
                "expected: 8, got: %u.", info.CM);
        return false;
    }

    if((uInfo.og.CMF * 256 + uInfo.og.FLG) % 31 != 0)
    {
        DS_ERROR("Failed zlib header integrity check: CMF*256 + FLG "
                "is not a multiple of 31, but instead: %u.",
                uInfo.og.CMF * 256 + uInfo.og.FLG);
        return false;
    }

    DS_DEBUG("CM:     %u", info.CM);
    DS_DEBUG("CINFO:  %u", info.CINFO);
    DS_DEBUG("FCHECK: %u", info.FCHECK);
    DS_DEBUG("FDICT:  %u", info.FDICT);
    DS_DEBUG("FLEVEL: %u", info.FLEVEL);
    DS_DEBUG("DICTID: %u", uInfo.og.DICTID);

    uint32_t madeChecksum = 0;

    uint64_t bytesRead = dsReadDeflate(zlib, dest, info.CINFO, info.FCHECK, info.FDICT,
                                       &madeChecksum);

    uint32_t readChecksum = *(uint32_t*)(&zlib[3 + bytesRead]);

    DS_DEBUG("made checksum: %u", madeChecksum);
    DS_DEBUG("read checksum: %u", readChecksum);

    return madeChecksum == readChecksum;
}
