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
    if(cap < 3)
    {
        PD_ERROR("cannot read less than 3 bytes of zlib data. max passed: %lu.", cap);
    }

    ZLibUnion uInfo = { .data = {zlib[0], zlib[1], zlib[2]} };
    ZlibInfo  info  = uInfo.info;

    if(info.CM != 8)
    {
        PD_ERROR("could not validate CMF in zlib data. "
                 "expected: 8, got: %u.", info.CM);
        return (DeflateInfo){0};
    }

    PD_TRACE("CMF: 0x%X", uInfo.og.CMF);
    PD_TRACE("FLG: 0x%X", uInfo.og.FLG);

    uint16_t header = (uint16_t)(uInfo.og.CMF << 8) | uInfo.og.FLG;
    if(header % 31 != 0)
    {
        PD_ERROR("Failed zlib header integrity check: CMF*256 + FLG "
                 "is not a multiple of 31, but %u.",
                 header);
        return (DeflateInfo){0};
    }

    PD_TRACE("CM:     %u", info.CM);
    PD_TRACE("CINFO:  %u", info.CINFO);
    PD_TRACE("FCHECK: %u", info.FCHECK);
    PD_TRACE("FDICT:  %u", info.FDICT);
    PD_TRACE("FLEVEL: %u", info.FLEVEL);

    // uint32_t DICTID       = 0;
    uint32_t madeChecksum = 0;
    uint32_t readChecksum = 0;

    DeflateInfo defInfo = {0};

    if(info.FDICT)
    {
        // DICTID = *(uint32_t*)(&zlib[2]);
        defInfo = dsReadDeflate(&zlib[6], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[6 + defInfo.compressedBytesRead] << 24;
        readChecksum += (uint32_t)zlib[7 + defInfo.compressedBytesRead] << 16;
        readChecksum += (uint32_t)zlib[8 + defInfo.compressedBytesRead] << 8;
        readChecksum += (uint32_t)zlib[9 + defInfo.compressedBytesRead];
        defInfo.compressedBytesRead += 10;
    }
    else
    {
        defInfo = dsReadDeflate(&zlib[2], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[2 + defInfo.compressedBytesRead] << 24;
        readChecksum += (uint32_t)zlib[3 + defInfo.compressedBytesRead] << 16;
        readChecksum += (uint32_t)zlib[4 + defInfo.compressedBytesRead] << 8;
        readChecksum += (uint32_t)zlib[5 + defInfo.compressedBytesRead];
        defInfo.compressedBytesRead += 6;
    }

    // PD_DEBUG("DICTID: 0x%X", DICTID);
    PD_DEBUG("made checksum: 0x%X", madeChecksum);
    PD_DEBUG("read checksum: 0x%X", readChecksum);
    PD_DEBUG("read a total of %lu compressed bytes.", defInfo.compressedBytesRead);
    PD_DEBUG("wrote a total of %lu decompressed bytes.", defInfo.bytesWritten);

    if(madeChecksum != readChecksum)
    {
        PD_WARN("made checksum (0x%X) does not match the read checksum (0x%X).",
                 madeChecksum, readChecksum);
    }

    return defInfo;
}
