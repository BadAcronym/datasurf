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
    }
    og;
}
ZLibUnion;

DeflateInfo dsReadZlibPtr
(
    const uint8_t *zlib,
    uint8_t       *dest,
    uint64_t      cap
){
    DeflateInfo defInfo = {0};

    if(cap < 3)
    {
        PD_ERROR("cannot read less than 3 bytes of zlib data. max passed: %"PRIu64".",
                 cap);
        return (DeflateInfo){0};
    }
    ZLibUnion uInfo = { .data = {zlib[0], zlib[1], zlib[2]} };
    ZlibInfo  info  = uInfo.info;

    if(info.CM != 8)
    {
        PD_ERROR("could not validate CMF in zlib data. expected: 8, got: %"
                 PRIu32".", info.CM);
        return (DeflateInfo){0};
    }

    PD_TRACE("CMF: 0x%X", uInfo.og.CMF);
    PD_TRACE("FLG: 0x%X", uInfo.og.FLG);

    uint16_t header = (uint16_t)(uInfo.og.CMF << 8) | uInfo.og.FLG;
    if(header % 31 != 0)
    {
        PD_ERROR("Failed zlib header integrity check: CMF*256 + FLG is not a multiple "
                 "of 31, but %"PRIu32".", header);
        return (DeflateInfo){0};
    }

    PD_TRACE("CM:     %"PRIu8, info.CM);
    PD_TRACE("CINFO:  %"PRIu8, info.CINFO);
    PD_TRACE("FCHECK: %"PRIu8, info.FCHECK);
    PD_TRACE("FDICT:  %"PRIu8, info.FDICT);
    PD_TRACE("FLEVEL: %"PRIu8, info.FLEVEL);

    uint32_t DICTID       = 0;
    uint32_t madeChecksum = 0;
    uint32_t readChecksum = 0;

    if(info.FDICT)
    {
        DICTID = *(uint32_t*)(&zlib[2]);

        //
        PD_ERROR("unknown zlib dictionary ID: %"PRIu32, DICTID);
        return defInfo;
        //

        // later, if entries are ever handled
        defInfo = dsReadDeflate(&zlib[6], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[6 + defInfo.bytesRead] << 24;
        readChecksum += (uint32_t)zlib[7 + defInfo.bytesRead] << 16;
        readChecksum += (uint32_t)zlib[8 + defInfo.bytesRead] << 8;
        readChecksum += (uint32_t)zlib[9 + defInfo.bytesRead];
        defInfo.bytesRead += 10;
    }
    else
    {
        defInfo = dsReadDeflate(&zlib[2], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[2 + defInfo.bytesRead] << 24;
        readChecksum += (uint32_t)zlib[3 + defInfo.bytesRead] << 16;
        readChecksum += (uint32_t)zlib[4 + defInfo.bytesRead] << 8;
        readChecksum += (uint32_t)zlib[5 + defInfo.bytesRead];
        defInfo.bytesRead += 6;
    }

    PD_DEBUG("made checksum: 0x%X", madeChecksum);
    PD_DEBUG("read checksum: 0x%X", readChecksum);
    PD_DEBUG("read a total of %"PRIu64" compressed bytes.", defInfo.bytesRead);
    PD_DEBUG("wrote a total of %"PRIu64" decompressed bytes.", defInfo.bytesWritten);

    if(madeChecksum != readChecksum)
    {
        PD_WARN("made checksum (0x%X) does not match the read checksum (0x%X).",
                 madeChecksum, readChecksum);
        defInfo.success = false;
    }

    return defInfo;
}
