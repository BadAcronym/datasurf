#include "datasurf_main.h"
#include "pd_print_macros.h"

int main
(
    void
){
    uint8_t uncompressed[39] =
    {
        0x01, 0x22, 0x00, 0xDD, 0xFF, 0x48, 0x65,
        0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72,
        0x6C, 0x64, 0x2C, 0x20, 0x74, 0x68, 0x69,
        0x73, 0x20, 0x69, 0x73, 0x20, 0x75, 0x6E,
        0x63, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73,
        0x73, 0x65, 0x64, 0x2E
    };
    uint8_t  testUncompressed[39] = {0};
    uint32_t checksumUncompressed = 0x21750EA2;
    uint32_t checksum             = 0;

    uint64_t bytesRead = dsReadDeflate(uncompressed, testUncompressed, 0, 0, 0,
                                       &checksum);

    if(!bytesRead)
    {
        PD_ERROR("could not read uncompressed string: no bytes read.");
    }
    else if(checksum != checksumUncompressed)
    {
        PD_ERROR("precomputed checksum %x does not match received checksum %x",
                 checksumUncompressed, checksum);
    }

    return 0;
}
