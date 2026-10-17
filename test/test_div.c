/* rg_div_u64 and rg_div_u64_u32 (helpers/rg_div.c) against the native
 * 64-bit division. */
#include <stdio.h>
#include <stdlib.h>

#include "../helpers/rg_div.h"

static int checks, failures;

static void check64(uint64_t n, uint64_t d) {
    checks++;
    uint64_t want = d ? n / d : UINT64_MAX;
    if(rg_div_u64(n, d) != want) {
        failures++;
        if(failures < 10)
            printf("FAIL %llu / %llu\n", (unsigned long long)n, (unsigned long long)d);
    }
}

static void check(uint64_t n, uint32_t d) {
    check64(n, d);
    checks++;
    uint32_t want = (d == 0 || n / d > UINT32_MAX) ? UINT32_MAX : (uint32_t)(n / d);
    uint32_t got = rg_div_u64_u32(n, d);
    if(got != want) {
        failures++;
        if(failures < 10)
            printf(
                "FAIL %llu / %lu: %lu, want %lu\n",
                (unsigned long long)n,
                (unsigned long)d,
                (unsigned long)got,
                (unsigned long)want);
    }
}

static uint64_t g_state = 0x9E3779B97F4A7C15ull;
static uint64_t rnd64(void) {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return g_state;
}

int main(void) {
    printf("test_div\n");
    static const uint64_t ns[] = {0, 1, 2, 99, 100, 101, 999, 1000, 0xFFFFFFFFull, 0x100000000ull,
                                  0x1FFFFFFFFull, 0xFFFFFFFF00000000ull, UINT64_MAX};
    static const uint32_t ds[] = {0, 1, 2, 3, 7, 10, 100, 1000, 0x7FFFFFFFu, 0x80000000u,
                                  0xFFFFFFFEu, 0xFFFFFFFFu};
    for(size_t i = 0; i < sizeof(ns) / sizeof(ns[0]); i++)
        for(size_t j = 0; j < sizeof(ds) / sizeof(ds[0]); j++)
            check(ns[i], ds[j]);
    // Quotients just below and at 2^32.
    for(uint32_t d = 1; d < 2000; d++) {
        check((uint64_t)d * 0xFFFFFFFFull + (d - 1), d);
        check((uint64_t)d << 32, d);
    }
    // Random dividends of every width over random divisors.
    for(int i = 0; i < 2000000; i++) {
        uint64_t n = rnd64() >> (rnd64() % 64);
        uint32_t d = (uint32_t)(rnd64() >> (32 + rnd64() % 32));
        check(n, d);
    }
    // 64-bit divisors of every width.
    for(int i = 0; i < 2000000; i++) {
        uint64_t n = rnd64() >> (rnd64() % 64);
        uint64_t d = rnd64() >> (rnd64() % 64);
        check64(n, d);
    }
    for(int s = 0; s < 64; s++) {
        check64(UINT64_MAX, 1ull << s);
        check64(UINT64_MAX, (1ull << s) - 1u);
        check64((1ull << s), (1ull << s) + 1u);
        check64(UINT64_MAX - 1u, UINT64_MAX);
    }
    // The app's shapes: percentages and averages.
    for(int i = 0; i < 200000; i++) {
        uint32_t total = (uint32_t)rnd64() | 1u, done = (uint32_t)(rnd64() % ((uint64_t)total + 1));
        checks++;
        if(rg_muldiv_u32(done, 100u, total) != (uint32_t)((uint64_t)done * 100u / total)) failures++;
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
