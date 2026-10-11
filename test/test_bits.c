/**
 * Host-side unit tests for the Bitstream Explorer maths (helpers/rg_bits.c)
 * over the analyzer's frames of synthetic captures (test/synth.h).
 * Run: make -C test
 */
#include "../helpers/rg_bits.h"
#include "synth.h"
#include <stdio.h>
#include <stdlib.h>
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

static RgAnalysis g_r;
static int32_t g_buf[20000];

/* A frame built straight from a '0'/'1' string. */
static RgFrame frame_of(const char* bits, uint8_t fit) {
    RgFrame f;
    memset(&f, 0, sizeof(f));
    size_t n = strlen(bits);
    f.bit_count = (uint16_t)n;
    f.fit = fit;
    f.group = RG_ANALYZER_NO_GROUP;
    for(size_t i = 0; i < n; i++)
        if(bits[i] == '1') f.bits[i / 8] |= (uint8_t)(0x80u >> (i % 8));
    return f;
}

static void analyze(const char* const* frames, size_t count, int repeats) {
    size_t pos = 0;
    for(int r = 0; r < repeats; r++)
        for(size_t i = 0; i < count; i++)
            synth_pwm(g_buf, &pos, frames[i], 3);
    rg_analyzer_run(g_buf, pos, &g_r);
}

static void test_bits_and_reference(void) {
    printf("test_bits_and_reference\n");
    RgFrame f = frame_of("1011", 100);
    CHECK(rg_bits_bit(&f, 0) == 1 && rg_bits_bit(&f, 1) == 0 && rg_bits_bit(&f, 3) == 1, "bits");
    CHECK(rg_bits_bit(&f, 4) == -1 && rg_bits_bit(&f, 1000) == -1, "beyond the frame: none");
    RgFrame noisy = frame_of("1011", RG_ANALYZER_GOOD_FIT - 1);
    CHECK(rg_bits_usable(&f) && !rg_bits_usable(&noisy), "usable needs a clean decode");

    // Two patterns: A sent 3x, B once. The reference is pattern A.
    const char* frames[] = {
        "101100111000101011110000",
        "101100111000101011110000",
        "101100111000101011110011",
        "101100111000101011110000"};
    analyze(frames, 4, 1);
    CHECK(g_r.frames_kept == 4, "4 frames kept");
    size_t ref = rg_bits_reference(&g_r);
    CHECK(ref < g_r.frames_kept && rg_bits_pattern(&g_r, ref) == 'A', "reference is pattern A");
    CHECK(rg_bits_repeats(&g_r, ref) == 3, "A repeats 3 times");
    CHECK(rg_bits_repeats(&g_r, 2) == 1 && rg_bits_pattern(&g_r, 2) == 'B', "B once");
    int shift = 9;
    CHECK(rg_bits_similarity(&g_r.frames[ref], &g_r.frames[0], &shift) == 100, "identical: 100");
    CHECK(shift == 0, "no shift");
    // 2 of 24 bits differ.
    CHECK(rg_bits_similarity(&g_r.frames[ref], &g_r.frames[2], NULL) == 22 * 100 / 24, "22 of 24");
    RgFrame empty = frame_of("", 100);
    CHECK(rg_bits_similarity(&f, &empty, NULL) == 0, "empty frame: 0");
    CHECK(rg_bits_pattern(&g_r, 99) == '-' && rg_bits_repeats(&g_r, 99) == 0, "out of range");

    RgAnalysis none;
    memset(&none, 0, sizeof(none));
    CHECK(rg_bits_reference(&none) == 0, "no frames: frames_kept");
}

static void test_alignment(void) {
    printf("test_alignment\n");
    // A frame that lost its first bit matches the reference one bit later.
    RgFrame a = frame_of("110010101111000011001010", 100);
    RgFrame b = frame_of("10010101111000011001010", 100);
    int shift = 0;
    int sim = rg_bits_similarity(&a, &b, &shift);
    CHECK(shift == 1, "aligned one bit in");
    CHECK(sim == 23 * 100 / 24, "the missing bit counts against it");
    // The analyzer groups such a shifted frame with its pattern.
    const char* frames[] = {
        "110010101111000011001010", "110010101111000011001010", "10010101111000011001010"};
    analyze(frames, 3, 1);
    CHECK(g_r.frames_kept == 3, "3 frames");
    CHECK(rg_bits_pattern(&g_r, 2) == 'A', "shifted frame joins pattern A");
    CHECK(rg_bits_repeats(&g_r, 0) == 3, "pattern A: 2 exact + 1 shifted");
    // It is not comparable bit by bit (different length): diff ignores it.
    CHECK(rg_bits_comparable(&g_r, 0) == 2, "two same-length frames");
    CHECK(rg_bits_comparable(&g_r, 2) == 1, "the shorter one alone");
}

static void test_diff(void) {
    printf("test_diff\n");
    // Bits 20..23 change between frames (a button field); the rest is fixed.
    const char* frames[] = {
        "101100111000101011110001",
        "101100111000101011110010",
        "101100111000101011110100",
        "101100111000101011111000"};
    analyze(frames, 4, 2);
    CHECK(g_r.frames_kept == 8, "8 frames");
    char markers[RG_ANALYZER_MAX_BITS + 1];
    size_t c = 0, x = 0, u = 0;
    size_t n = rg_bits_diff(&g_r, 0, markers, &c, &x, &u);
    CHECK(n == 8, "8 comparable frames");
    CHECK(strcmp(markers, "....................XXXX") == 0, "bits 20-23 change");
    CHECK(c == 20 && x == 4 && u == 0, "counts");

    // Identical frames only: everything constant (never 'changing').
    const char* same[] = {"110011001010"};
    analyze(same, 1, 3);
    n = rg_bits_diff(&g_r, 0, markers, &c, &x, &u);
    CHECK(n == 3 && x == 0 && c == 12 && strcmp(markers, "............") == 0, "all constant");

    // One frame: no evidence, every position unknown, nothing inferred.
    analyze(same, 1, 1);
    n = rg_bits_diff(&g_r, 0, markers, &c, &x, &u);
    CHECK(n == 1 && u == 12 && c == 0 && x == 0, "single frame: unknown");
    CHECK(strcmp(markers, "????????????") == 0, "all '?'");

    // Out-of-range selection: nothing.
    n = rg_bits_diff(&g_r, 100, markers, &c, &x, &u);
    CHECK(n == 0 && markers[0] == '\0' && c + x + u == 0, "no frame");

    // A noisy or cut-off frame of the same length is not evidence.
    memset(&g_r, 0, sizeof(g_r));
    g_r.frames[0] = frame_of("1100", 100);
    g_r.frames[1] = frame_of("1101", RG_ANALYZER_GOOD_FIT - 1);
    g_r.frames[2] = frame_of("1110", 100);
    g_r.frames[2].truncated = true;
    g_r.frames_kept = 3;
    n = rg_bits_diff(&g_r, 0, markers, &c, &x, &u);
    CHECK(n == 1 && strcmp(markers, "????") == 0, "noisy and cut-off frames excluded");
}

static void test_bytes(void) {
    printf("test_bytes\n");
    // 21 bits: 2 whole bytes and 5 trailing bits at offset 0.
    RgFrame f = frame_of("101010111100110111101", 100);
    RgBitsBytes l;
    rg_bits_bytes(&f, 0, &l);
    CHECK(l.lead == 0 && l.bytes == 2 && l.tail == 5, "21 bits: 2 bytes + 5");
    uint8_t v = 0;
    CHECK(rg_bits_byte(&f, 0, 0, &v) && v == 0xAB, "byte 0 = AB");
    CHECK(rg_bits_byte(&f, 0, 1, &v) && v == 0xCD, "byte 1 = CD");
    CHECK(!rg_bits_byte(&f, 0, 2, &v), "the 5 tail bits are not a byte (no padding)");
    // Shifted grid: 3 lead bits, then bytes from bit 3.
    rg_bits_bytes(&f, 3, &l);
    CHECK(l.lead == 3 && l.bytes == 2 && l.tail == 2, "offset 3: 3 + 2 bytes + 2");
    CHECK(rg_bits_byte(&f, 3, 0, &v) && v == 0x5E, "01011110");
    CHECK(rg_bits_byte(&f, 3, 1, &v) && v == 0x6F, "01101111");
    // The offset wraps at 8 and never exceeds the frame.
    rg_bits_bytes(&f, 11, &l);
    CHECK(l.lead == 3, "offset taken mod 8");
    RgFrame tiny = frame_of("10", 100);
    rg_bits_bytes(&tiny, 5, &l);
    CHECK(l.lead == 2 && l.bytes == 0 && l.tail == 0, "lead capped at the frame");
    RgFrame exact = frame_of("1111000000001111", 100);
    rg_bits_bytes(&exact, 0, &l);
    CHECK(l.bytes == 2 && l.tail == 0 && l.lead == 0, "byte aligned");
}

static void test_fields(void) {
    printf("test_fields\n");
    RgFrame f = frame_of("101010111100110111101", 100);
    RgBitsField fld;
    char bin[80], hex[24];
    CHECK(rg_bits_field(&f, 0, 7, &fld) == RgBitsFieldOk, "field 0-7");
    CHECK(fld.length == 8 && fld.has_value && fld.value == 0xAB, "AB");
    rg_bits_field_hex(&fld, hex, sizeof(hex));
    CHECK(strcmp(hex, "AB") == 0, "hex AB");
    // Non-byte-aligned field: 5 bits from bit 3 = 01011 = 11 = 0xB.
    CHECK(rg_bits_field(&f, 3, 7, &fld) == RgBitsFieldOk && fld.value == 11, "5-bit value");
    rg_bits_field_hex(&fld, hex, sizeof(hex));
    CHECK(strcmp(hex, "0B") == 0, "two hex digits for 5 bits, MSB first");
    CHECK(rg_bits_field_binary(&f, &fld, bin, sizeof(bin)) && strcmp(bin, "01011") == 0, "bin");
    // The trailing bits are a field of their own.
    CHECK(rg_bits_field(&f, 16, 20, &fld) == RgBitsFieldOk && fld.value == 0x1D, "tail 11101");
    // One bit.
    CHECK(
        rg_bits_field(&f, 20, 20, &fld) == RgBitsFieldOk && fld.value == 1 && fld.length == 1,
        "1 bit");
    rg_bits_field_hex(&fld, hex, sizeof(hex));
    CHECK(strcmp(hex, "1") == 0, "hex of one bit");
    // Out of range: refused, nothing invented.
    CHECK(rg_bits_field(&f, 0, 21, &fld) == RgBitsFieldRange, "end beyond the frame");
    CHECK(rg_bits_field(&f, 9, 8, &fld) == RgBitsFieldRange, "start after end");
    RgFrame empty = frame_of("", 100);
    CHECK(rg_bits_field(&empty, 0, 0, &fld) == RgBitsFieldEmpty, "empty frame");
    // 64 bits have a value, 65 do not (binary still shown).
    char long_bits[100];
    for(int i = 0; i < 70; i++)
        long_bits[i] = (i % 3 == 0) ? '1' : '0';
    long_bits[70] = '\0';
    RgFrame big = frame_of(long_bits, 100);
    CHECK(rg_bits_field(&big, 0, 63, &fld) == RgBitsFieldOk && fld.has_value, "64-bit value");
    uint64_t want = 0;
    for(int i = 0; i < 64; i++)
        want = (want << 1) | (i % 3 == 0);
    CHECK(fld.value == want, "64-bit value exact");
    rg_bits_field_hex(&fld, hex, sizeof(hex));
    CHECK(strlen(hex) == 16, "16 hex digits");
    CHECK(
        rg_bits_field(&big, 0, 64, &fld) == RgBitsFieldOk && !fld.has_value, "65 bits: no value");
    rg_bits_field_hex(&fld, hex, sizeof(hex));
    CHECK(hex[0] == '\0', "no hex without a value");
    // Binary that does not fit says so instead of cutting silently.
    char small[10];
    CHECK(!rg_bits_field_binary(&big, &fld, small, sizeof(small)), "binary cut");
    CHECK(strlen(small) == 9 && strcmp(small + 7, "..") == 0, "ends in ..");
    CHECK(rg_bits_field_binary(&big, &fld, bin, sizeof(bin)) && strlen(bin) == 65, "fits");
}

int main(void) {
    test_bits_and_reference();
    test_alignment();
    test_diff();
    test_bytes();
    test_fields();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
