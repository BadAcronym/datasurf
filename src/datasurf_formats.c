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

bool dsReadZlibPtr
(
    const uint8_t *zlib,
    uint8_t       *dest,
    uint64_t      cap
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

    // uint32_t DICTID       = 0;
    uint32_t madeChecksum = 0;
    uint32_t readChecksum = 0;
    uint64_t compressedBytesRead = 0;

    if(info.FDICT)
    {
        // DICTID = *(uint32_t*)(&zlib[2]);
        compressedBytesRead = dsReadDeflate(&zlib[6], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[6 + compressedBytesRead] << 24;
        readChecksum += (uint32_t)zlib[7 + compressedBytesRead] << 16;
        readChecksum += (uint32_t)zlib[8 + compressedBytesRead] << 8;
        readChecksum += (uint32_t)zlib[9 + compressedBytesRead];
    }
    else
    {
        compressedBytesRead = dsReadDeflate(&zlib[2], dest, &madeChecksum, cap);
        readChecksum += (uint32_t)zlib[2 + compressedBytesRead] << 24;
        readChecksum += (uint32_t)zlib[3 + compressedBytesRead] << 16;
        readChecksum += (uint32_t)zlib[4 + compressedBytesRead] << 8;
        readChecksum += (uint32_t)zlib[5 + compressedBytesRead];
    }

    if(!compressedBytesRead)
    {
        PD_ERROR("couldn't read data from provided deflate stream.");
        return 0;
    }

    // PD_DEBUG("DICTID: 0x%X", DICTID);
    PD_DEBUG("made checksum: 0x%X", madeChecksum);
    PD_DEBUG("read checksum: 0x%X", readChecksum);
    PD_DEBUG("read a total of %lu compressed bytes.", compressedBytesRead);

    if(madeChecksum != readChecksum)
    {
        PD_WARN("made checksum (0x%X) does not match the read checksum (0x%X).",
                 madeChecksum, readChecksum);
    }

    return true;
}
