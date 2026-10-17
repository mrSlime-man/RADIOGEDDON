/**
 * @file rg_div.h
 * @brief 64-bit division without the compiler's 64-bit division helper.
 *
 * Dividing a uint64_t on the Cortex-M4 calls __aeabi_uldivmod and links
 * __udivmoddi4 (about 750 bytes, in the resident app and again in every
 * module that divides 64-bit values) for a handful of percentages, means
 * and unit conversions. These do the same divisions exactly: the hardware's
 * 32-bit divide when both values fit, else a shift-and-subtract over the
 * quotient's bits only (64-bit shifts, compares and subtractions, and the
 * leading-zero count, are inline instructions).
 *
 * Pure C; host-tested against the native division (test/test_div.c).
 */
#pragma once

#include <stdint.h>

/** floor(@p n / @p d), exactly; UINT64_MAX when @p d is 0. */
uint64_t rg_div_u64(uint64_t n, uint64_t d);

/** floor(@p n / @p d) saturated at UINT32_MAX (never wrapped); @p d == 0
 * gives UINT32_MAX. */
static inline uint32_t rg_div_u64_u32(uint64_t n, uint64_t d) {
    uint64_t q = rg_div_u64(n, d);
    return q > UINT32_MAX ? UINT32_MAX : (uint32_t)q;
}

/** floor(@p a * @p b / @p d), saturated as above: a percentage or a rate. */
static inline uint32_t rg_muldiv_u32(uint32_t a, uint32_t b, uint32_t d) {
    return rg_div_u64_u32((uint64_t)a * b, d);
}
