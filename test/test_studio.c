/**
 * Host tests for Pulse Studio's logic (helpers/rg_studio.c): the
 * pulse-to-bit map both ways, pulses that belong to no bit, uncertain
 * symbols, the waveform window (zoom, pan keeping the cursor visible),
 * glitch and preamble marks, the timing histogram and field differences.
 * Signals are synthetic, built from known bits.
 * Run: make -C test
 */
#include "../helpers/rg_studio.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

#define TE 400
static RgStudioFrame g_f;

static size_t pwm(int32_t* s, const char* bits, int preamble) {
    size_t n = 0;
    for(int i = 0; i < preamble; i++)
        s[n++] = i % 2 ? -TE : TE;
    if(preamble) s[n - 1] = -4 * TE; // header gap closes the preamble
    for(const char* b = bits; *b; b++) {
        s[n++] = *b == '1' ? 3 * TE : TE;
        s[n++] = *b == '1' ? -TE : -3 * TE;
    }
    s[n++] = TE; // stop pulse
    return n;
}

static void test_map_both_ways(void) {
    printf("test_map_both_ways\n");
    int32_t s[200];
    const char* bits = "1011001110";
    size_t n = pwm(s, bits, 0);
    RgDecodeParams p = {0};
    p.te_us = TE;
    p.pwm_short_us = TE;
    p.pwm_long_us = 3 * TE;
    rg_studio_load(&g_f, s, n, RgEncodingPWM, &p);
    CHECK(g_f.nbits == 10 && strcmp(g_f.bits, bits) == 0, "bits read");
    CHECK(g_f.span_us == 10u * 4 * TE + TE, "span");
    bool unsure = true;
    for(uint16_t b = 0; b < g_f.nbits; b++) {
        uint16_t first, end;
        CHECK(rg_studio_bit_span(&g_f, b, &first, &end), "span");
        CHECK(first == 2 * b && end == 2 * b + 2, "pulse and gap");
        CHECK(rg_studio_bit_of(&g_f, first, &unsure) == b && !unsure, "pulse to bit");
        CHECK(rg_studio_bit_of(&g_f, first + 1, NULL) == b, "gap to bit");
    }
    CHECK(
        rg_studio_bit_of(&g_f, (uint16_t)(n - 1), NULL) == RG_STUDIO_NO_BIT, "stop pulse: no bit");
    CHECK(rg_studio_bit_of(&g_f, 999, NULL) == RG_STUDIO_NO_BIT, "past the end");
    CHECK(!rg_studio_bit_span(&g_f, 10, NULL, NULL), "no such bit");
    // A broken symbol is mapped but uncertain.
    s[4] = TE;
    s[5] = -TE;
    rg_studio_load(&g_f, s, n, RgEncodingPWM, &p);
    CHECK(rg_studio_bit_of(&g_f, 4, &unsure) == 2 && unsure, "uncertain bit");
    // Without an encoding nothing is mapped.
    rg_studio_load(&g_f, s, n, RgEncodingUnknown, &p);
    CHECK(
        g_f.nbits == 0 && rg_studio_bit_of(&g_f, 0, NULL) == RG_STUDIO_NO_BIT, "unknown: no map");
}

static void test_preamble_and_glitch(void) {
    printf("test_preamble_and_glitch\n");
    int32_t s[200];
    size_t n = pwm(s, "110100", 12);
    RgDecodeParams p = {0};
    p.te_us = TE;
    p.pwm_short_us = TE;
    p.pwm_long_us = 3 * TE;
    p.skip_square = true;
    rg_studio_load(&g_f, s, n, RgEncodingPWM, &p);
    CHECK(g_f.preamble == 11, "preamble cells counted");
    CHECK(rg_studio_bit_of(&g_f, 3, NULL) == RG_STUDIO_NO_BIT, "preamble is no bit");
    CHECK(strcmp(g_f.bits, "110100") == 0, "data after the preamble");
    CHECK(rg_studio_bit_of(&g_f, 12, NULL) == 0, "first data pulse is bit 0");
    // Same-level samples are merged as the analyzer merges them.
    int32_t m[] = {TE, 2 * TE, -TE, -TE, 50, -3 * TE};
    rg_studio_load(&g_f, m, 6, RgEncodingUnknown, &p);
    CHECK(g_f.n == 4 && g_f.s[0] == 3 * TE && g_f.s[1] == -2 * TE, "merged");
    CHECK(rg_studio_glitch(&g_f, 2) && !rg_studio_glitch(&g_f, 0), "glitch marked");
}

static void test_window(void) {
    printf("test_window\n");
    int32_t s[200];
    size_t n = pwm(s, "10110011101000111010", 0);
    RgDecodeParams p = {0};
    p.te_us = TE;
    p.pwm_short_us = TE;
    p.pwm_long_us = 3 * TE;
    rg_studio_load(&g_f, s, n, RgEncodingPWM, &p);
    uint8_t z = rg_studio_zoom_fit(&g_f, 120);
    CHECK((uint64_t)rg_studio_zoom_us(z) * 120 >= g_f.span_us, "fit shows all");
    CHECK(z == 0 || (uint64_t)rg_studio_zoom_us(z - 1) * 120 < g_f.span_us, "closest that fits");
    // At the closest zoom, moving the cursor pans just enough.
    uint64_t start = 0;
    for(uint16_t c = 0; c < g_f.n; c++) {
        start = rg_studio_pan(&g_f, start, 0, 120, c);
        uint64_t a = rg_studio_time_at(&g_f, c);
        CHECK(a >= start && a < start + 1200u, "cursor visible");
    }
    CHECK(start + 1200u <= g_f.span_us + 1, "never past the end");
    start = rg_studio_pan(&g_f, start, 0, 120, 0);
    CHECK(start == 0, "back to the start");
    CHECK(rg_studio_pan(&g_f, 5000, 7, 120, 3) == 0, "whole frame on screen: no pan");
    CHECK(
        rg_studio_sample_at(&g_f, 0) == 0 && rg_studio_sample_at(&g_f, 3 * TE) == 1,
        "time to sample");
    CHECK(rg_studio_sample_at(&g_f, 1u << 30) == g_f.n - 1, "clamped");
    CHECK(rg_studio_time_at(&g_f, 2) == 4 * TE, "sample to time");
}

static void test_hist_and_diff(void) {
    printf("test_hist_and_diff\n");
    int32_t s[200];
    size_t n = pwm(s, "1100", 0);
    RgDecodeParams p = {0};
    p.te_us = TE;
    p.pwm_short_us = TE;
    p.pwm_long_us = 3 * TE;
    rg_studio_load(&g_f, s, n, RgEncodingPWM, &p);
    uint8_t hi[RG_STUDIO_HIST], lo[RG_STUDIO_HIST];
    rg_studio_hist(&g_f, hi, lo);
    CHECK(hi[1] == 3 && hi[5] == 2, "highs at 1 Te (two zeros, stop) and 3 Te");
    CHECK(lo[1] == 2 && lo[5] == 2, "lows at 1 Te and 3 Te");
    uint8_t mask[(RG_ANALYZER_MAX_BITS + 7) / 8];
    CHECK(rg_studio_diff(&g_f, "1000", 4, mask) == 1 && (mask[0] & 2), "one bit differs");
    CHECK(rg_studio_diff(&g_f, "11", 2, mask) == 2, "bits past the reference differ");
}

int main(void) {
    test_map_both_ways();
    test_preamble_and_glitch();
    test_window();
    test_hist_and_diff();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
