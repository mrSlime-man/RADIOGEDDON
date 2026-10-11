#include "rg_crc.h"

uint32_t rg_crc32(uint32_t crc, const uint8_t* data, size_t n) {
    crc = ~crc;
    for(size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for(int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
