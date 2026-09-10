#include "datasurf_main.h"
#include "pd_print_macros.h"

int main
(
    void
){
    uint8_t failed = 0;
    uint8_t uncompressed[46] =
    {
        //zlib header
        0x78, 0x01,
        // deflate stream
        0x01, 0x22, 0x00, 0xDD, 0xFF, 0x48, 0x65,
        0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72,
        0x6C, 0x64, 0x2C, 0x20, 0x74, 0x68, 0x69,
        0x73, 0x20, 0x69, 0x73, 0x20, 0x75, 0x6E,
        0x63, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73,
        0x73, 0x65, 0x64, 0x2E,
        // ADLER-32 precomputed checksum of uncompressed data:
        0xDA, 0x3D, 0x0C, 0xA3
    };
    uint8_t testUncompressed[39] = {0};

    if(!dsReadZlibPtr(uncompressed, testUncompressed))
    {
        PD_FAIL("dsReadZlibPtr: could not read uncompressed string.");
        ++failed;
    }
    else
    {
        PD_SUCCESS("passed dsReadZlibPtr with uncompressed zlib string.");
    }

    return failed;
}
