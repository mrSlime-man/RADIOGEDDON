#include "rg_div.h"

/* Leading zeros of a non-zero 64-bit value with the 32-bit CLZ instruction
 * (GCC may call a library routine for the 64-bit builtin). */
static int rg_div_clz64(uint64_t x) {
    uint32_t hi = (uint32_t)(x >> 32);
    return hi ? __builtin_clz(hi) : 32 + __builtin_clz((uint32_t)x);
}

uint64_t rg_div_u64(uint64_t n, uint64_t d) {
    if(d == 0) return UINT64_MAX;
    if(!((n | d) >> 32)) return (uint32_t)n / (uint32_t)d; // one UDIV instruction
    if(n < d) return 0;
    // Line the divisor up under the dividend's top bit, then one quotient
    // bit per step (at most 64 steps, usually far fewer).
    int shift = rg_div_clz64(d) - rg_div_clz64(n);
    d <<= shift;
    uint64_t q = 0;
    for(; shift >= 0; shift--) {
        q <<= 1;
        if(n >= d) {
            n -= d;
            q |= 1u;
        }
        d >>= 1;
    }
    return q;
}
