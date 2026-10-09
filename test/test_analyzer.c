/**
 * Host-side unit tests for the firmware-independent analysis engine
 * (helpers/rg_analyzer.c). Build & run via `make -C test check`.
 */
#include "../helpers/rg_analyzer.h"
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

#define TE 350

/* Emit a Princeton-style PWM frame for `bits` (LSB table order as given) into
 * buf at *pos: '0' = short-high/long-low, '1' = long-high/short-low, then a
 * long sync gap. Returns via *pos. */
static void emit_princeton(int32_t* buf, size_t* pos, const char* bits) {
    for(const char* b = bits; *b; b++) {
        if(*b == '1') {
            buf[(*pos)++] = 3 * TE; /* high */
            buf[(*pos)++] = -TE; /* low */
        } else {
            buf[(*pos)++] = TE; /* high */
            buf[(*pos)++] = -3 * TE; /* low */
        }
    }
    buf[(*pos)++] = -31 * TE; /* inter-frame sync gap */
}

static void test_pwm_identification(void) {
    printf("test_pwm_identification\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "101010110011000011110001"; /* 24 bits */
    for(int i = 0; i < 4; i++)
        emit_princeton(buf, &pos, key);

    RgAnalysis a;
    rg_analyzer_run(buf, pos, &a);

    CHECK(a.encoding == RgEncodingPWM, "encoding classified as PWM/OOK");
    CHECK(a.encoding_confidence >= 50, "PWM confidence is meaningful");
    CHECK(a.te_us >= 300 && a.te_us <= 400, "Te estimated near 350us");
    CHECK(a.frame_count == 4, "four frames segmented on sync gaps");
    CHECK(a.frames_repeat, "repeated frames detected");
    CHECK(a.repeat_count == 4, "all four repeats counted");
    CHECK(a.bit_count == 24, "24 bits extracted from representative frame");
    CHECK(strcmp(a.bits, key) == 0, "decoded bits match the emitted key");
}

static void test_fixed_code_fields(void) {
    printf("test_fixed_code_fields\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "110100101011000011110000";
    for(int i = 0; i < 3; i++)
        emit_princeton(buf, &pos, key);

    RgAnalysis a;
    rg_analyzer_run(buf, pos, &a);
    CHECK(a.have_field_diff, "field diff computed for repeated frames");
    CHECK(a.changing_bits == 0, "fixed code: no changing bits across presses");
    CHECK(a.const_bits == 24, "fixed code: all 24 bits constant");
}

static void test_rolling_code_fields(void) {
    printf("test_rolling_code_fields\n");
    /* Two presses sharing a constant ID prefix but a changing counter suffix. */
    int32_t buf[2048];
    size_t pos = 0;
    const char* press_a = "101010101010"
                          "000000000000";
    const char* press_b = "101010101010"
                          "000000000111"; /* last 3 bits differ */
    emit_princeton(buf, &pos, press_a);
    emit_princeton(buf, &pos, press_b);

    RgAnalysis a;
    rg_analyzer_run(buf, pos, &a);
    CHECK(a.frames_repeat, "two equal-length frames treated as repeats");
    CHECK(a.have_field_diff, "field diff computed");
    CHECK(a.changing_bits == 3, "exactly the 3 differing bits flagged as changing");
    CHECK(a.const_bits == 21, "remaining 21 bits reported constant (ID portion)");
    CHECK(
        a.field_map[21] == 'X' && a.field_map[23] == 'X' && a.field_map[0] == '.',
        "field map marks the changing suffix, not the constant prefix");
}

static void test_similarity(void) {
    printf("test_similarity\n");
    int32_t a[256], b[256], c[256];
    size_t pa = 0, pb = 0, pc = 0;
    emit_princeton(a, &pa, "101010110011000011110001");
    emit_princeton(b, &pb, "101010110011000011110001"); /* identical */
    emit_princeton(c, &pc, "010101001100111100001110"); /* inverted-ish */

    CHECK(rg_analyzer_similarity(a, pa, b, pb) >= 95, "identical captures score very high");
    CHECK(rg_analyzer_similarity(a, pa, c, pc) < 70, "different captures score lower");
    CHECK(rg_analyzer_similarity(a, pa, a, 0) == 0, "empty comparand scores 0");
}

static void test_degenerate(void) {
    printf("test_degenerate\n");
    RgAnalysis a;
    rg_analyzer_run(NULL, 0, &a);
    CHECK(a.sample_count == 0 && a.encoding == RgEncodingUnknown, "empty input is safe");
    CHECK(a.bit_count == 0 && a.bits[0] == '\0', "no bits from empty input");

    int32_t one[1] = {350};
    rg_analyzer_run(one, 1, &a);
    CHECK(a.sample_count == 1, "single-sample input handled");
}

static void test_ppm_shape(void) {
    printf("test_ppm_shape\n");
    /* Constant short high pulse, information in variable gap -> PPM shape. */
    int32_t buf[512];
    size_t pos = 0;
    for(int rep = 0; rep < 2; rep++) {
        for(int i = 0; i < 20; i++) {
            buf[pos++] = TE; /* fixed high */
            buf[pos++] = (i % 2 == 0) ? -2 * TE : -4 * TE; /* variable gap */
        }
        buf[pos++] = -31 * TE;
    }
    RgAnalysis a;
    rg_analyzer_run(buf, pos, &a);
    CHECK(
        a.encoding == RgEncodingPPM || a.encoding == RgEncodingPWM,
        "constant-pulse/variable-gap classified as PPM (or PWM fallback)");
    CHECK(a.encoding_confidence > 0, "a hypothesis with non-zero confidence is produced");
}

int main(void) {
    test_pwm_identification();
    test_fixed_code_fields();
    test_rolling_code_fields();
    test_similarity();
    test_degenerate();
    test_ppm_shape();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("ANALYZER TESTS FAILED\n");
        return 1;
    }
    printf("ALL ANALYZER TESTS PASSED\n");
    return 0;
}
