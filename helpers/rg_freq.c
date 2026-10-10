#include "rg_freq.h"

#include <stdio.h>

void rg_freq_text(uint32_t hz, char* out, size_t out_size) {
    if(!out || out_size == 0) return;
    unsigned long mhz = (unsigned long)(hz / 1000000u);
    unsigned long khz = (unsigned long)((hz % 1000000u) / 1000u);
    if(khz % 10u == 0) {
        snprintf(out, out_size, "%lu.%02lu", mhz, khz / 10u);
    } else {
        snprintf(out, out_size, "%lu.%03lu", mhz, khz);
    }
}

bool rg_freq_in_range(uint32_t hz) {
    return hz >= RG_FREQ_MIN_HZ && hz <= RG_FREQ_MAX_HZ;
}

size_t rg_freq_nearest(const uint32_t* list, size_t n, uint32_t hz) {
    size_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for(size_t i = 0; i < n; i++) {
        uint32_t d = list[i] > hz ? list[i] - hz : hz - list[i];
        if(d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

size_t rg_freq_find(const uint32_t* list, size_t n, uint32_t hz) {
    for(size_t i = 0; i < n; i++) {
        if(list[i] == hz) return i;
    }
    return n;
}
