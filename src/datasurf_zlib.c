#include "datasurf_main.h"
#include "ds_print_macros.h"

#include <inttypes.h>

typedef struct ZLibInfo
{
    u08 CM     : 4;
    u08 CINFO  : 4;
    u08 FCHECK : 5;
    u08 FDICT  : 1;
    u08 FLEVEL : 2;
}
ZlibInfo;

typedef union ZLibUnion
{
    u08      data[3];
    ZlibInfo info;
    struct
    {
        u08  CMF;
        u08  FLG;
    }
    og;
}
ZLibUnion;

DeflateInfo dsReadZlibPtr
(
    const u08 *zlib,
    u08       *dest,
    u64       cap
){
    DeflateInfo defInfo = {0};

    if(cap < 3)
    {
        DS_ERROR("cannot read less than 3 bytes of zlib data. max passed: %"PRIu64".",
                 cap);
        return (DeflateInfo){0};
    }
    ZLibUnion uInfo = { .data = {zlib[0], zlib[1], zlib[2]} };
    ZlibInfo  info  = uInfo.info;

    if(info.CM != 8)
    {
        DS_ERROR("could not validate CMF in zlib data. expected: 8, got: %"
                 PRIu32".", info.CM);
        return (DeflateInfo){0};
    }

    DS_TRACE("CMF: 0x%X", uInfo.og.CMF);
    DS_TRACE("FLG: 0x%X", uInfo.og.FLG);

    u16 header = (u16)(uInfo.og.CMF << 8) | uInfo.og.FLG;
    if(header % 31 != 0)
    {
        DS_ERROR("Failed zlib header integrity check: CMF*256 + FLG is not a multiple "
                 "of 31, but %"PRIu32".", header);
        return (DeflateInfo){0};
    }

    DS_TRACE("CM:     %"PRIu8, info.CM);
    DS_TRACE("CINFO:  %"PRIu8, info.CINFO);
    DS_TRACE("FCHECK: %"PRIu8, info.FCHECK);
    DS_TRACE("FDICT:  %"PRIu8, info.FDICT);
    DS_TRACE("FLEVEL: %"PRIu8, info.FLEVEL);

    u32 DICTID       = 0;
    u32 madeChecksum = 0;
    u32 readChecksum = 0;

    if(info.FDICT)
    {
        DICTID = *(u32*)(&zlib[2]);

        //
        DS_ERROR("unknown zlib dictionary ID: %"PRIu32, DICTID);
        return defInfo;
        //
    }
    else
    {
        defInfo = dsReadDeflate(&zlib[2], dest, &madeChecksum, cap);
        u08 *trailer = (u08*)(zlib + 2 + defInfo.bytesRead);

        readChecksum = (u32)trailer[0] << 24 |
                       (u32)trailer[1] << 16 |
                       (u32)trailer[2] << 8  |
                       (u32)trailer[3];
        defInfo.bytesRead += 4;
    }

    DS_DEBUG("made checksum: 0x%"PRIX32, madeChecksum);
    DS_DEBUG("read checksum: 0x%"PRIX32, readChecksum);
    DS_DEBUG("read a total of %"PRIu64" compressed bytes.", defInfo.bytesRead);
    DS_DEBUG("wrote a total of %"PRIu64" decompressed bytes.", defInfo.bytesWritten);

    if(madeChecksum != readChecksum)
    {
        DS_WARN("made checksum (0x%X) does not match the read checksum (0x%X).",
                 madeChecksum, readChecksum);
        defInfo.success = false;
    }

    return defInfo;
}
