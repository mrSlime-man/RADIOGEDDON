/**
 * Host-side unit tests for Multi-Capture Compare (helpers/rg_multi.c) on
 * synthetic multi-capture datasets with known field differences
 * (test/synth.h), each capture analysed by the real analyzer.
 * Run: make -C test
 */
#include "../helpers/rg_multi.h"
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
static int32_t g_buf[40000];
static RgMultiCapture g_caps[RG_MULTI_MAX_CAPTURES];
static RgMultiResult g_res;
static char g_report[8192];

/* One RAW capture: @p frame sent @p repeats times, analysed. */
static void
    capture(RgMultiCapture* cap, const char* name, const char* frame, int repeats, uint32_t hz) {
    size_t pos = 0;
    for(int i = 0; i < repeats; i++)
        synth_pwm(g_buf, &pos, frame, 3);
    rg_analyzer_run(g_buf, pos, &g_r);
    rg_multi_capture_init(cap, name);
    cap->frequency = hz;
    strcpy(cap->preset, "AM650");
    rg_multi_capture_from_analysis(cap, &g_r);
}

static const char* report(size_t n) {
    RgText t;
    rg_text_init(&t, g_report, sizeof(g_report));
    rg_multi_report(g_caps, n, &g_res, &t);
    return g_report;
}

static void test_buttons(void) {
    printf("test_buttons\n");
    // A fixed-code remote: a 20-bit constant part, a 4-bit one-hot button.
    const char* f[] = {
        "101100111000101011110001",
        "101100111000101011110010",
        "101100111000101011110100",
        "101100111000101011111000"};
    for(int i = 0; i < 4; i++)
        capture(&g_caps[i], "btn", f[i], 3, 433920000);
    CHECK(g_caps[0].source == RgMultiSourceRaw && g_caps[0].bit_count == 24, "24-bit frames");
    CHECK(g_caps[0].patterns == 1 && g_caps[0].pattern[0].count == 3, "one pattern x3");
    rg_multi_compare(g_caps, 4, &g_res);
    CHECK(g_res.usable == 4 && g_res.compared == 4, "all compared");
    CHECK(g_res.same_frequency && g_res.same_preset && g_res.same_te, "same freq/preset/Te");
    CHECK(g_res.same_encoding && g_res.same_length && g_res.length == 24, "same encoding/length");
    CHECK(g_res.const_bits == 20 && g_res.changing_bits == 4, "20 constant, 4 changing");
    CHECK(g_res.runs == 2, "two runs");
    CHECK(g_res.run[0].kind == RgMultiRunConstant && g_res.run[0].length == 20, "constant 0-19");
    CHECK(g_res.run[1].kind == RgMultiRunButton && g_res.run[1].start == 20, "button 20-23");
    CHECK(g_res.run[1].values == 4, "four values");
    CHECK(g_res.distinct_frames == 4 && g_res.single_frame_captures == 0, "4 frames, repeated");
    CHECK(g_res.similarity == (22 * 100 / 24), "similarity: 2 bits differ per pair");
    const char* r = report(4);
    CHECK(strstr(r, "button/command-like") != NULL, "button reported");
    CHECK(strstr(r, "Bits 0-19 constant") && strstr(r, "not verified as a serial"), "ID hedged");
    CHECK(strstr(r, "unlikely to be noise") != NULL, "repeats: not noise");
    CHECK(
        strstr(r, "[OBSERVED]") && strstr(r, "[HEURISTIC]") && strstr(r, "[HYPOTHESIS]"),
        "labels");
    CHECK(strstr(r, "serial number") == NULL, "never claims a serial number");
    CHECK(strstr(r, "....................XXXX") != NULL, "bit map");

    // The same button twice: one letter for both.
    capture(&g_caps[4], "btn1 again", f[0], 3, 433920000);
    rg_multi_compare(g_caps, 5, &g_res);
    CHECK(g_res.same_as[0] == 'A' && g_res.same_as[4] == 'A', "same frame, same letter");
    CHECK(g_res.distinct_frames == 4, "still 4 different frames");
    CHECK(g_res.run[1].values == 4, "four distinct values over five captures");
}

static void test_counter_and_rolling(void) {
    printf("test_counter_and_rolling\n");
    // An 8-bit counter in bits 16-23: 10, 11, 12, 14 in capture order.
    const char* f[] = {
        "1011001110001010"
        "00001010",
        "1011001110001010"
        "00001011",
        "1011001110001010"
        "00001100",
        "1011001110001010"
        "00001110"};
    for(int i = 0; i < 4; i++)
        capture(&g_caps[i], "cnt", f[i], 2, 433920000);
    rg_multi_compare(g_caps, 4, &g_res);
    CHECK(g_res.runs >= 2, "runs");
    bool counter = false;
    for(size_t i = 0; i < g_res.runs; i++)
        if(g_res.run[i].kind == RgMultiRunCounter) counter = true;
    CHECK(counter, "counter-like run found");
    CHECK(strstr(report(4), "counter-like") != NULL, "counter reported");
    // Out of order: not a counter.
    capture(&g_caps[1], "cnt", f[3], 2, 433920000);
    capture(&g_caps[3], "cnt", f[1], 2, 433920000);
    rg_multi_compare(g_caps, 4, &g_res);
    counter = false;
    for(size_t i = 0; i < g_res.runs; i++)
        if(g_res.run[i].kind == RgMultiRunCounter) counter = true;
    CHECK(!counter, "unordered values: no counter");

    // A long changing block (rolling-code-like): reported as data, no rule.
    const char* rl[] = {
        "1100110011110000"
        "1011010010110101",
        "1100110011110000"
        "0110100101101110",
        "1100110011110000"
        "1110001010010011"};
    for(int i = 0; i < 3; i++)
        capture(&g_caps[i], "roll", rl[i], 2, 433920000);
    rg_multi_compare(g_caps, 3, &g_res);
    bool varies = false;
    for(size_t i = 0; i < g_res.runs; i++)
        if(g_res.run[i].kind == RgMultiRunVaries) varies = true;
    CHECK(varies, "long changing run: no simple rule");
    CHECK(strstr(report(3), "rolling") != NULL, "rolling/encrypted hedge");
}

static void test_inconsistent(void) {
    printf("test_inconsistent\n");
    capture(&g_caps[0], "a", "101100111000101011110001", 3, 433920000);
    capture(&g_caps[1], "b", "101100111000101011110001", 3, 433950000); // within tolerance
    capture(&g_caps[2], "c", "1011001110001010111100011100", 3, 315000000);
    strcpy(g_caps[2].preset, "FM238");
    rg_multi_compare(g_caps, 3, &g_res);
    CHECK(!g_res.same_frequency && !g_res.same_preset && !g_res.same_length, "differences");
    CHECK(g_res.length == 24 && g_res.compared == 2, "the common length compared");
    const char* r = report(3);
    CHECK(strstr(r, "differs") != NULL, "differences reported");
    rg_multi_compare(g_caps, 2, &g_res);
    CHECK(g_res.same_frequency, "30 kHz apart: same channel");
    CHECK(g_res.changing_bits == 0, "identical frames");
    CHECK(strstr(report(2), "same frame") != NULL, "same frame noted");

    // A capture with its frame only once: noise warning.
    capture(&g_caps[1], "once", "101100111000101011110011", 1, 433920000);
    rg_multi_compare(g_caps, 2, &g_res);
    CHECK(g_res.single_frame_captures == 1, "single frame counted");
    CHECK(strstr(report(2), "may be\nnoise") != NULL, "noise warning");

    // Unusable captures: noise only, or never analysed.
    rg_multi_capture_init(&g_caps[0], "empty");
    size_t pos = 0;
    for(int i = 0; i < 200; i++)
        g_buf[pos++] = (i % 2) ? -(int32_t)(100 + synth_rng() % 5000) :
                                 (int32_t)(100 + synth_rng() % 5000);
    rg_analyzer_run(g_buf, pos, &g_r);
    rg_multi_capture_from_analysis(&g_caps[0], &g_r);
    rg_multi_capture_init(&g_caps[1], "unread");
    rg_multi_compare(g_caps, 2, &g_res);
    CHECK(g_res.usable == 0 && g_res.similarity == -1, "nothing usable");
    CHECK(strstr(report(2), "No capture has a frame") != NULL, "explained");
    rg_multi_compare(g_caps, 0, &g_res);
    CHECK(g_res.captures == 0 && g_res.usable == 0, "zero captures");
}

static void test_decoded_and_text(void) {
    printf("test_decoded_and_text\n");
    rg_multi_capture_init(&g_caps[0], "key1");
    rg_multi_capture_from_key(&g_caps[0], "Princeton", 24, 0xB38AF1);
    rg_multi_capture_init(&g_caps[1], "key2");
    rg_multi_capture_from_key(&g_caps[1], "Princeton", 24, 0xB38AF2);
    CHECK(
        rg_multi_bit(&g_caps[0], 0, 0) == 1 && rg_multi_bit(&g_caps[0], 0, 23) == 1,
        "key bits MSB first");
    CHECK(rg_multi_bit(&g_caps[0], 0, 24) == -1 && rg_multi_bit(&g_caps[0], 1, 0) == -1, "beyond");
    rg_multi_compare(g_caps, 2, &g_res);
    CHECK(
        g_res.changing_bits == 2 && g_res.single_frame_captures == 0,
        "2 bits change, no noise flag");
    CHECK(strstr(report(2), "[CONFIRMED] decoder") != NULL, "decoded files marked");
    rg_multi_capture_from_key(&g_caps[1], "Princeton", 100, ~0ull);
    CHECK(g_caps[1].bit_count == 64, "key bits capped at 64");

    // A tiny buffer: the report is cut and says so, never overflows.
    char small[64];
    RgText t;
    rg_text_init(&t, small, sizeof(small));
    rg_multi_report(g_caps, 2, &g_res, &t);
    CHECK(t.truncated && strlen(small) < sizeof(small), "cut, inside the buffer");
    rg_text_init(&t, small, 0);
    rg_text_printf(&t, "x");
    CHECK(t.len == 0, "zero-size buffer");

    // More than RG_MULTI_PATTERNS patterns in one capture: the most frequent kept.
    size_t pos = 0;
    const char* p[] = {
        "110011001100110011001101",
        "110011001100110011001110",
        "110011001100110011000111",
        "110011001100110011001011",
        "110011001100110011010101"};
    for(int i = 0; i < 5; i++)
        for(int k = 0; k < (i == 4 ? 4 : 1); k++)
            synth_pwm(g_buf, &pos, p[i], 2);
    rg_analyzer_run(g_buf, pos, &g_r);
    rg_multi_capture_init(&g_caps[0], "many");
    rg_multi_capture_from_analysis(&g_caps[0], &g_r);
    CHECK(g_caps[0].patterns == RG_MULTI_PATTERNS, "patterns capped");
    CHECK(g_caps[0].pattern[0].count == 4, "most frequent first");
    CHECK(g_caps[0].pattern[1].count == 1, "then the rest");
}

int main(void) {
    test_buttons();
    test_counter_and_rolling();
    test_inconsistent();
    test_decoded_and_text();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
