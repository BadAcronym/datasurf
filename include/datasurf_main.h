#ifndef DATASURF_H
#define DATASURF_H

#include <stdint.h>
#include <stdbool.h>

bool dsReadZlibPtr
(
    uint8_t  *zlib,
    uint8_t  *dest,
    uint64_t maxDeflateLen
);

bool dsReadDeflate
(
    uint8_t *src,
    uint8_t *dst,
    uint8_t CINFO,
    uint8_t FCHECK,
    uint8_t FDICT
);

#endif
