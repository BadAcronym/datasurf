#include "datasurf_main.h"
#include "datasurf_info_macros.h"

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

typedef union /*ZUnion*/{
	uint8_t data[3];

	ZlibInfo info;

	struct{
		uint8_t CMF;
		uint8_t FLG;
		uint8_t DICTID;
	}orig;
}ZUnion;

bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest,
    uint64_t maxDeflateLen
){
	ZUnion uinfo = { .data = {zlib[0], zlib[1], zlib[2]} };
	ZlibInfo info = uinfo.info;
	uint8_t CMF    = zlib[0];//can be replaced by uinfo.orig.CMF
	uint8_t FLG    = zlib[1];//can be replaced by uinfo.orig.FLG
	uint8_t DICTID = zlib[2];//can be replaced by uinfo.orig.DICTID

    if(info.CM != 8)
    {
    	DATASURF_ERROR(stderr, "could not validate CMF in zlib data. "
                "expected: 8, got: %u.", info.CM);
        return false;
    }

    if((CMF * 256 + FLG) % 31 != 0)
    {
    	DATASURF_ERROR(stderr, "Failed zlib header integrity check: CMF*256 + FLG "
                "is not a multiple of 31, but instead: %u.",
                CMF * 256 + FLG);
        return false;
    }

    DATASURF_DEBUG(stdout, "CM:     %u", info.CM);
    DATASURF_DEBUG(stdout, "CINFO:  %u", info.CINFO);
    DATASURF_DEBUG(stdout, "FCHECK: %u", info.FCHECK);
    DATASURF_DEBUG(stdout, "FDICT:  %u", info.FDICT);
    DATASURF_DEBUG(stdout, "FLEVEL: %u", info.FLEVEL);
    DATASURF_DEBUG(stdout, "DICTID: %u", DICTID);

    uint32_t madeChecksum = 0;

    uint64_t bytesRead = dsReadDeflate(zlib, dest, info.CINFO, info.FCHECK, info.FDICT,
                                       &madeChecksum);

    uint32_t readChecksum = *(uint32_t*)(&zlib[3 + bytesRead]);

    DATASURF_DEBUG(stdout, "made checksum: %u", madeChecksum);
    DATASURF_DEBUG(stdout, "read checksum: %u", readChecksum);

    return madeChecksum == readChecksum;
}
