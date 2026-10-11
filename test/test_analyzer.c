/**
 * Host-side unit tests for the firmware-independent analysis engine
 * (helpers/rg_analyzer.c). Build & run via `make -C test check`.
 *
 * Every signal here is SYNTHETIC: generated in code from a known bit pattern
 * so the expected answer is exact. They show the engine recovers structure it
 * was given; they are not evidence about real captures from a Flipper.
 */
#include "../helpers/rg_analyzer.h"
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

#define TE 350

static RgAnalysis g_a; /* large; keep off the stack */

/* Deterministic pseudo-random source (LCG) for noise and jitter. */
static uint32_t g_rng = 12345u;
static uint32_t rng(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7FFFu;
}

/* Princeton-style PWM: '0' = Te high / 3Te low, '1' = 3Te high / Te low, then
 * a Te stop pulse and a 31 Te sync gap. jitter_pct > 0 perturbs each edge. */
static void emit_pwm(int32_t* buf, size_t* pos, const char* bits, int jitter_pct) {
    for(const char* b = bits; *b; b++) {
        int32_t hi = (*b == '1') ? 3 * TE : TE;
        int32_t lo = (*b == '1') ? TE : 3 * TE;
        if(jitter_pct) {
            hi +=
                (int32_t)((int64_t)hi * ((int)(rng() % (2 * jitter_pct + 1)) - jitter_pct) / 100);
            lo +=
                (int32_t)((int64_t)lo * ((int)(rng() % (2 * jitter_pct + 1)) - jitter_pct) / 100);
        }
        buf[(*pos)++] = hi;
        buf[(*pos)++] = -lo;
    }
    buf[(*pos)++] = TE;
    buf[(*pos)++] = -31 * TE;
}

/* PPM: constant 500 us pulse, gap 1000 us = '0', 2000 us = '1', 12 ms frame gap. */
static void emit_ppm(int32_t* buf, size_t* pos, const char* bits) {
    for(const char* b = bits; *b; b++) {
        buf[(*pos)++] = 500;
        buf[(*pos)++] = (*b == '1') ? -2000 : -1000;
    }
    buf[(*pos)++] = 500;
    buf[(*pos)++] = -12000;
}

/* Manchester (IEEE 802.3: '1' = low then high, '0' = high then low) with a
 * 400 us half-bit, idle low before and a 10 ms gap after. Equal adjacent
 * half-cells merge into one longer pulse, as a receiver sees them. */
static void emit_manchester(int32_t* buf, size_t* pos, const char* bits) {
    int level = 0; /* idle low */
    int32_t run = 0;
    for(const char* b = bits; *b; b++) {
        int cells[2] = {(*b == '1') ? 0 : 1, (*b == '1') ? 1 : 0};
        for(int c = 0; c < 2; c++) {
            if(cells[c] == level) {
                run += 400;
            } else {
                if(run) buf[(*pos)++] = level ? run : -run;
                level = cells[c];
                run = 400;
            }
        }
    }
    if(level) {
        buf[(*pos)++] = run;
        buf[(*pos)++] = -10000;
    } else {
        buf[(*pos)++] = -(run + 10000);
    }
}

/* Receiver noise: log-uniform durations 30..~1900 us, alternating level. */
static void emit_noise(int32_t* buf, size_t* pos, size_t n) {
    for(size_t i = 0; i < n; i++) {
        uint32_t k = rng() % 600u;
        uint32_t d = 30u << (k / 100u);
        d += d * (k % 100u) / 100u;
        buf[(*pos)++] = (i % 2) ? -(int32_t)d : (int32_t)d;
    }
}

static void test_pwm_identification(void) {
    printf("test_pwm_identification\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "101010110011000011110001"; /* 24 bits */
    for(int i = 0; i < 4; i++)
        emit_pwm(buf, &pos, key, 0);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "encoding classified as PWM");
    CHECK(g_a.encoding_confidence >= 80, "clean PWM gets high confidence");
    CHECK(g_a.encoding_confidence <= 95, "confidence never claims certainty");
    CHECK(g_a.te_us >= 330 && g_a.te_us <= 370, "Te estimated near 350us");
    CHECK(g_a.high_peak_count == 2, "two high-pulse timing peaks observed");
    CHECK(g_a.low_peak_count == 3, "three low peaks observed (Te, 3Te, sync gap)");
    CHECK(g_a.frame_count == 4, "four frames segmented on sync gaps");
    CHECK(g_a.signal_frames == 4, "all four frames decode cleanly");
    CHECK(g_a.group_count == 1 && g_a.groups[0].exact == 4, "four identical repeats");
    CHECK(g_a.bit_count == 24 && g_a.bit_count_frames == 4, "bit length 24 in 4/4 frames");
    CHECK(strcmp(g_a.bits, key) == 0, "decoded bits match the emitted key");
    CHECK(g_a.fit_pct == 100, "every symbol fits the PWM grammar");
    CHECK(g_a.noise_pct == 0 && g_a.jitter_pct == 0, "synthetic timing is clean");
    CHECK(g_a.quality == RgQualityGood, "clean signal graded good");
    CHECK(g_a.frames_kept == 4 && g_a.frames[1].start_index == 50, "frame 2 starts at sample 50");
    CHECK(g_a.frames[1].start_us == 24ull * 4 * TE + 32 * TE, "frame 2 start time measured");
}

static void test_fixed_code_fields(void) {
    printf("test_fixed_code_fields\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "110100101011000011110000";
    for(int i = 0; i < 3; i++)
        emit_pwm(buf, &pos, key, 0);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.have_field_diff && g_a.compared == 2, "field diff over the other two frames");
    CHECK(g_a.changing_bits == 0, "fixed code: no changing bits across repeats");
    CHECK(g_a.const_bits == 24, "fixed code: all 24 bits constant");
    CHECK(g_a.id_len == 0, "no ID candidate without changing bits");
}

static void test_changing_fields(void) {
    printf("test_changing_fields\n");
    /* Two presses sharing a constant prefix but a changing suffix. */
    int32_t buf[2048];
    size_t pos = 0;
    emit_pwm(buf, &pos, "101010101010000000000000", 0);
    emit_pwm(buf, &pos, "101010101010000000000111", 0);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.group_count == 2, "two distinct patterns");
    CHECK(g_a.have_field_diff, "field diff computed");
    CHECK(g_a.changing_bits == 3, "exactly the 3 differing bits flagged as changing");
    CHECK(g_a.const_bits == 21, "remaining 21 bits reported constant");
    CHECK(
        g_a.field_map[21] == 'X' && g_a.field_map[23] == 'X' && g_a.field_map[0] == '.',
        "field map marks the changing suffix, not the constant prefix");
    CHECK(g_a.id_start == 0 && g_a.id_len == 21, "ID candidate is the 21-bit constant run");
}

static void test_ppm_identification(void) {
    printf("test_ppm_identification\n");
    int32_t buf[1024];
    size_t pos = 0;
    const char* key = "1100101011110000";
    for(int i = 0; i < 3; i++)
        emit_ppm(buf, &pos, key);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPPM, "constant pulse + two gap widths classified PPM");
    CHECK(g_a.params.ppm_high_us == 500, "pulse width measured");
    CHECK(
        g_a.params.ppm_short_us == 1000 && g_a.params.ppm_long_us == 2000,
        "the two gap widths measured");
    CHECK(strcmp(g_a.bits, key) == 0, "PPM bits recovered (long gap = 1)");
    CHECK(g_a.groups[0].exact == 3, "three identical PPM frames");
}

static void test_manchester_identification(void) {
    printf("test_manchester_identification\n");
    int32_t buf[2048];
    size_t pos = 0;
    /* Starts with '1' so the first half-cell merges into the idle low. */
    const char* key = "10110010111000011010010110110001";
    for(int i = 0; i < 3; i++)
        emit_manchester(buf, &pos, key);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingManchester, "1/2 Te transitions classified Manchester");
    CHECK(g_a.te_us >= 380 && g_a.te_us <= 420, "half-bit Te near 400us");
    CHECK(g_a.frame_count == 3, "three Manchester frames");
    CHECK(strcmp(g_a.bits, key) == 0, "Manchester bits recovered (IEEE convention)");
    CHECK(g_a.groups[0].exact == 3, "three identical Manchester frames");
    CHECK(g_a.fit_pct == 100, "no Manchester violations");

    /* Starting with '0' needs the other phase. */
    pos = 0;
    const char* key0 = "01001101000111100101101001001110";
    for(int i = 0; i < 2; i++)
        emit_manchester(buf, &pos, key0);
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingManchester, "Manchester starting with 0 identified");
    CHECK(strcmp(g_a.bits, key0) == 0, "bits recovered when the frame starts high");
}

/* Glitches: the receiver's ~65 us blips, more numerous than either real
 * width, between the frames. They are observed as a peak but are never Te. */
static void test_glitch_floor(void) {
    printf("test_glitch_floor\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "110100101100111000101101";
    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 120; k++)
            buf[pos++] = (k % 2) ? -66 : 65;
        buf[pos++] = -20000;
        emit_pwm(buf, &pos, key, 0);
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.high_peak_count > 0 && g_a.high_peaks[0].center_us < 100, "glitch peak observed");
    CHECK(g_a.te_us >= 330 && g_a.te_us <= 370, "Te is the signal's, not the glitches'");
    CHECK(
        g_a.params.pwm_short_us == TE && g_a.params.pwm_long_us == 3 * TE,
        "widths ignore glitches");
    CHECK(g_a.encoding == RgEncodingPWM && strcmp(g_a.bits, key) == 0, "frames still decode");
}

/* Noisy captures (the firmware's Holtek and Phoenix V2 test files) also show
 * a glitch peak at 132 us, two receiver sampling steps: still never Te. */
static void test_glitch_132(void) {
    printf("test_glitch_132\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "110100101100111000101101";
    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 120; k++)
            buf[pos++] = (k % 2) ? -132 : 66;
        buf[pos++] = -20000;
        emit_pwm(buf, &pos, key, 0);
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.te_us >= 330 && g_a.te_us <= 370, "Te is the signal's, not the 132 us glitches'");
    CHECK(g_a.encoding == RgEncodingPWM && strcmp(g_a.bits, key) == 0, "frames still decode");
}

/* Low first, as CAME sends it: a start pulse, then each bit is a low then a
 * high ('0' = 2Te low / Te high, '1' = Te low / 2Te high). */
static void emit_low_first(int32_t* buf, size_t* pos, const char* bits) {
    buf[(*pos)++] = TE;
    for(const char* b = bits; *b; b++) {
        buf[(*pos)++] = (*b == '1') ? -TE : -2 * TE;
        buf[(*pos)++] = (*b == '1') ? 2 * TE : TE;
    }
    buf[(*pos)++] = -40 * TE;
}

static void test_low_first_pwm(void) {
    printf("test_low_first_pwm\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "100110100011";
    for(int i = 0; i < 6; i++)
        emit_low_first(buf, &pos, key);
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "low-first PWM is PWM, not Manchester");
    CHECK(g_a.bit_count == 12 && strcmp(g_a.bits, key) == 0, "bits paired low then high");
}

/* KeeLoq-like: a square-wave preamble, a 10 Te header gap, then the data. The
 * preamble is a frame of single Te cells and gives no vote. */
static void test_preamble_no_vote(void) {
    printf("test_preamble_no_vote\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "0110100111010001101011001110001011010011";
    for(int i = 0; i < 4; i++) {
        for(int k = 0; k < 24; k++)
            buf[pos++] = (k % 2) ? -TE : TE;
        buf[pos++] = -10 * TE;
        for(const char* b = key; *b; b++) {
            buf[pos++] = (*b == '1') ? 2 * TE : TE;
            buf[pos++] = (*b == '1') ? -TE : -2 * TE;
        }
        buf[pos++] = TE; /* stop pulse */
        buf[pos++] = -40 * TE;
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "preambles do not outvote the data");
    CHECK(g_a.bit_count == 40 && strcmp(g_a.bits, key) == 0, "data frames decoded");
}

/* Repeats only 5 Te apart (less than the 7 Te default gap): the rare longer
 * low is taken as the separator. */
static void test_back_to_back(void) {
    printf("test_back_to_back\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "1011001110001011";
    for(int i = 0; i < 8; i++) {
        for(const char* b = key; *b; b++) {
            buf[pos++] = (*b == '1') ? 2 * TE : TE;
            buf[pos++] = (*b == '1') ? -TE : -2 * TE;
        }
        buf[pos++] = TE;
        buf[pos++] = -5 * TE;
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.gap_us > 2 * TE && g_a.gap_us < 5 * TE, "separator found below the default gap");
    CHECK(g_a.frame_count == 8, "each repeat is its own frame");
    CHECK(g_a.bit_count == 16 && strcmp(g_a.bits, key) == 0, "frame length is one repeat");
}

/* Repeats with no separator at all, as FAAC SLH, Power Smart, Honeywell and
 * Revers RB2 captures show: the decode runs to RG_ANALYZER_MAX_BITS and the
 * frame is cut to one repeat by the bits' own period. */
static void test_gapless_repeats(void) {
    printf("test_gapless_repeats\n");
    static int32_t buf[8192];
    size_t pos = 0;
    const char* key = "1101000000011101000000101001010000011110011111010101001100011101";
    for(int i = 0; i < 8; i++) {
        for(const char* b = key; *b; b++) {
            buf[pos++] = (*b == '1') ? 2 * TE : TE;
            buf[pos++] = (*b == '1') ? -TE : -2 * TE;
        }
    }
    buf[pos++] = TE;
    buf[pos++] = -40 * TE;
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.frame_count == 1, "one frame: nothing to cut on");
    CHECK(g_a.bit_count == 64, "frame length is one repeat");
    CHECK(strcmp(g_a.bits, key) == 0, "the repeat itself");
    CHECK(g_a.frames[0].repeat_bits >= 2 * 64, "the frame says it was cut to its period");

    // A frame that ends on its own is never cut, however regular it is.
    pos = 0;
    const char* halves = "1011001110110011";
    for(int i = 0; i < 4; i++)
        emit_pwm(buf, &pos, halves, 0);
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.bit_count == 16, "self-similar short frame kept whole");
    CHECK(g_a.frames[0].repeat_bits == 0, "not marked as cut");
}

static void test_repeat_period(void) {
    printf("test_repeat_period\n");
    char bits[RG_ANALYZER_MAX_BITS + 1];
    const char* unit = "10110010011101";
    size_t n = 0;
    while(n + 1 < sizeof(bits)) {
        bits[n] = unit[n % 14];
        n++;
    }
    bits[n] = '\0';
    CHECK(rg_analyzer_repeat_period(bits, n) == 14, "period of a repeated unit");
    // A few flipped bits (noise) still repeat at 97 %.
    bits[30] = bits[30] == '1' ? '0' : '1';
    bits[100] = bits[100] == '1' ? '0' : '1';
    CHECK(rg_analyzer_repeat_period(bits, n) == 14, "period survives a little noise");
    // Too short for two repeats.
    CHECK(rg_analyzer_repeat_period(bits, 20) == 0, "fewer than two repeats");
    // A preamble or a run of one level is not information.
    memset(bits, '0', 200);
    bits[0] = '1';
    CHECK(rg_analyzer_repeat_period(bits, 200) == 0, "near-constant run refused");
    for(size_t i = 0; i < 200; i++)
        bits[i] = (i % 2) ? '1' : '0';
    CHECK(rg_analyzer_repeat_period(bits, 200) == 0, "square wave refused");
    // Pseudo-random bits do not repeat.
    for(size_t i = 0; i < 256; i++)
        bits[i] = (rng() & 1) ? '1' : '0';
    CHECK(rg_analyzer_repeat_period(bits, 256) == 0, "random bits do not repeat");
}

/* Repeats separated by a long carrier pulse instead of a gap (as Hormann
 * sends them). */
static void test_high_separator(void) {
    printf("test_high_separator\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "110010100111001010110100";
    for(int i = 0; i < 5; i++) {
        buf[pos++] = 30 * TE;
        buf[pos++] = -TE;
        for(const char* b = key; *b; b++) {
            buf[pos++] = (*b == '1') ? 2 * TE : TE;
            buf[pos++] = (*b == '1') ? -TE : -2 * TE;
        }
    }
    buf[pos++] = TE; /* stop pulse */
    buf[pos++] = -40 * TE;
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.frame_count == 5, "a long high ends a frame");
    CHECK(g_a.bit_count == 24 && strcmp(g_a.bits, key) == 0, "frames split at the long highs");
}

static void test_noise_robustness(void) {
    printf("test_noise_robustness\n");
    static int32_t buf[4096];
    size_t pos = 0;
    const char* key = "011010011100101000111010";
    g_rng = 777u;
    emit_noise(buf, &pos, 300);
    buf[pos++] = -20000; /* silence before the press */
    for(int i = 0; i < 5; i++)
        emit_pwm(buf, &pos, key, 0);
    emit_noise(buf, &pos, 300);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.noise_pct >= 20, "noise share is reported");
    CHECK(g_a.te_us >= 300 && g_a.te_us <= 400, "Te still found under noise");
    CHECK(g_a.encoding == RgEncodingPWM, "PWM still identified under noise");
    CHECK(strcmp(g_a.bits, key) == 0, "dominant pattern is the real frame, not noise");
    CHECK(g_a.groups[0].exact >= 4, "repeats grouped despite noise");
    CHECK(g_a.frame_count > g_a.signal_frames, "noise frames counted but not decoded");
}

static void test_jitter(void) {
    printf("test_jitter\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "111000101101001011000110";
    g_rng = 99u;
    for(int i = 0; i < 4; i++)
        emit_pwm(buf, &pos, key, 12);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "PWM identified with +-12% jitter");
    CHECK(strcmp(g_a.bits, key) == 0, "bits survive jitter");
    CHECK(g_a.jitter_pct > 0 && g_a.jitter_pct <= 15, "jitter measured, roughly the input");
}

static void test_truncated_first_frame(void) {
    printf("test_truncated_first_frame\n");
    /* The recording started mid-frame: the first 4 bits are missing. */
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "100110101100011101001011";
    emit_pwm(buf, &pos, key + 4, 0);
    for(int i = 0; i < 3; i++)
        emit_pwm(buf, &pos, key, 0);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.bit_count == 24 && g_a.bit_count_frames == 3, "modal length 24 (3 frames)");
    CHECK(strcmp(g_a.bits, key) == 0, "reference pattern is a full frame");
    CHECK(g_a.group_count == 1, "cut-off frame joins the group");
    CHECK(g_a.groups[0].exact == 3 && g_a.groups[0].aligned == 1, "3 exact + 1 aligned");
    CHECK(g_a.frames[0].group == 0 && g_a.frames[0].shift == 4, "aligned 4 bits in");
    CHECK(g_a.compared == 2 && g_a.changing_bits == 0, "field map over full frames only");
}

static void test_two_buttons(void) {
    printf("test_two_buttons\n");
    int32_t buf[4096];
    size_t pos = 0;
    const char* a = "101100111000101011110001";
    const char* b = "101100111000101011110100";
    for(int i = 0; i < 2; i++)
        emit_pwm(buf, &pos, b, 0);
    for(int i = 0; i < 3; i++)
        emit_pwm(buf, &pos, a, 0);

    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.group_count == 2, "two patterns");
    CHECK(g_a.groups[0].exact == 3 && g_a.groups[1].exact == 2, "largest group first");
    CHECK(strcmp(g_a.bits, a) == 0, "dominant pattern is the most repeated");
    CHECK(g_a.frames[0].group == 1 && g_a.frames[4].group == 0, "frames remapped to groups");
    CHECK(g_a.changing_bits == 2, "the two differing bits found across the groups");
}

static void test_streaming_equivalence(void) {
    printf("test_streaming_equivalence\n");
    int32_t buf[2048];
    size_t pos = 0;
    const char* key = "001011101010010110100111";
    for(int i = 0; i < 3; i++)
        emit_pwm(buf, &pos, key, 0);
    rg_analyzer_run(buf, pos, &g_a);

    /* Same data in odd-sized chunks, with one long high split in two. */
    static RgAnalyzer an;
    rg_analyzer_begin(&an);
    int passes = 0;
    do {
        passes++;
        for(size_t i = 0; i < pos; i += 7) {
            size_t n = pos - i < 7 ? pos - i : 7;
            if(i == 0) {
                int32_t split[2] = {buf[0] / 2, buf[0] - buf[0] / 2};
                rg_analyzer_feed(&an, split, 2);
                rg_analyzer_feed(&an, buf + 1, n - 1);
            } else {
                rg_analyzer_feed(&an, buf + i, n);
            }
        }
    } while(rg_analyzer_next_pass(&an));
    CHECK(passes == 3, "three passes over the data");
    CHECK(strcmp(an.result.bits, g_a.bits) == 0, "chunked feed gives the same bits");
    CHECK(an.result.sample_count == g_a.sample_count, "split pulse merged back");
    CHECK(an.result.encoding_confidence == g_a.encoding_confidence, "same confidence");
    rg_analyzer_feed(&an, buf, pos);
    CHECK(!rg_analyzer_next_pass(&an), "a finished analyzer stays finished");
}

static void test_align(void) {
    printf("test_align\n");
    int shift = 99;
    size_t ov = 0;
    const char* a = "1100101011110000";
    CHECK(rg_analyzer_align(a, 16, a, 16, 4, &shift, &ov) == 16, "identical: all match");
    CHECK(shift == 0 && ov == 16, "identical: zero shift");
    CHECK(rg_analyzer_align(a, 16, a + 3, 13, 4, &shift, &ov) == 13, "suffix matches");
    CHECK(shift == 3 && ov == 13, "suffix aligned 3 bits in");
    CHECK(rg_analyzer_align(a + 2, 14, a, 16, 4, &shift, &ov) == 14, "prefix-extended frame");
    CHECK(shift == -2, "negative shift when the frame has extra leading bits");
}

static void test_decode_api(void) {
    printf("test_decode_api\n");
    RgDecodeParams p = {0};
    char bits[RG_ANALYZER_MAX_BITS + 1];
    int fit = -1;
    int32_t pwm[] = {TE, -3 * TE, 3 * TE, -TE};
    CHECK(
        rg_analyzer_decode(RgEncodingPWM, &p, pwm, 4, bits, sizeof(bits) - 1, &fit) == 0 &&
            fit == 0,
        "decoder without its parameters reports no fit");
    p.pwm_short_us = TE;
    p.pwm_long_us = 3 * TE;
    CHECK(
        rg_analyzer_decode(RgEncodingPWM, &p, pwm, 4, bits, sizeof(bits) - 1, &fit) == 2,
        "2 bits");
    CHECK(strcmp(bits, "01") == 0 && fit == 100, "PWM 0 then 1");
    int32_t bad[] = {TE, -TE, TE, -TE};
    rg_analyzer_decode(RgEncodingPWM, &p, bad, 4, bits, sizeof(bits) - 1, &fit);
    CHECK(fit == 0, "wrong symbol period does not fit PWM");
    p.te_us = 400;
    int32_t man[] = {400, -800, 400, -400, 800}; /* H L L H L H H (+idle L) */
    size_t n = rg_analyzer_decode(RgEncodingManchester, &p, man, 5, bits, sizeof(bits) - 1, &fit);
    CHECK(n == 4 && strcmp(bits, "0110") == 0, "Manchester pairs decoded");
    CHECK(fit == 100, "clean Manchester fits fully");
}

static void test_similarity(void) {
    printf("test_similarity\n");
    int32_t a[256], b[256], c[256];
    size_t pa = 0, pb = 0, pc = 0;
    emit_pwm(a, &pa, "101010110011000011110001", 0);
    emit_pwm(b, &pb, "101010110011000011110001", 0); /* identical */
    emit_pwm(c, &pc, "010101001100111100001110", 0); /* inverted */

    CHECK(rg_analyzer_similarity(a, pa, b, pb) >= 95, "identical captures score very high");
    CHECK(rg_analyzer_similarity(a, pa, c, pc) < 70, "different captures score lower");
    CHECK(rg_analyzer_similarity(a, pa, a, 0) == 0, "empty comparand scores 0");

    RgSimilarity s;
    rg_similarity_init(&s);
    rg_similarity_feed(&s, a, b, 20);
    rg_similarity_feed(&s, a + 20, b + 20, pa - 20);
    rg_similarity_tail(&s, 0, pa);
    CHECK(rg_similarity_score(&s) == 50, "streamed: longer tail halves the score");
}

/* StarLine-style pulse widths: every bit's gap repeats its pulse (0 = Te
 * high + Te low, 1 = 2Te + 2Te). Only 1 and 2 Te durations, so Manchester's
 * grammar fits it perfectly; the pulse/gap pairing tells them apart. */
static void emit_equal_pwm(int32_t* buf, size_t* pos, const char* bits) {
    // Header as StarLine sends it: long pulse/gap pairs (4 Te), no data.
    for(int i = 0; i < 6; i++) {
        buf[(*pos)++] = 1000;
        buf[(*pos)++] = -1000;
    }
    for(const char* b = bits; *b; b++) {
        int32_t d = (*b == '1') ? 500 : 250;
        buf[(*pos)++] = d + (int32_t)(rng() % 31) - 15;
        buf[(*pos)++] = -(d + (int32_t)(rng() % 31) - 15);
    }
    buf[(*pos)++] = -12000;
}

static void test_pairing_equal(void) {
    printf("test_pairing_equal\n");
    static int32_t buf[8000];
    size_t pos = 0;
    const char* code = "1011001110001010111100001100101011001101011110000101101001110010";
    for(int i = 0; i < 6; i++)
        emit_equal_pwm(buf, &pos, code);
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "pulse widths, not Manchester");
    CHECK(g_a.encoding_by_pairing && g_a.params.pwm_equal, "read from the pairing, gap = pulse");
    CHECK(g_a.alternative == RgEncodingManchester, "Manchester kept as the other reading");
    CHECK(g_a.encoding_fit[RgEncodingManchester] >= 90, "Manchester's grammar did fit");
    CHECK(g_a.pair_same_pct >= 95 && g_a.pair_count >= 100, "pairs keep to one rule");
    CHECK(g_a.encoding_confidence <= 70, "confidence capped for a rule-based reading");
    // The last bit's gap runs into the frame gap, so its pulse looks like a
    // stop pulse: one bit short, as a protocol-blind reading must be.
    CHECK(g_a.bit_count + 1 == strlen(code), "frame length from the data bits, less the last");
    CHECK(strncmp(g_a.bits, code, g_a.bit_count) == 0, "bits recovered (1 = long pulse and gap)");
}

/* A 1:2 pulse-width code (constant period) after a long square-wave
 * preamble and a sync pulse, some frames sent back to back: the preamble is
 * no data and the pulses pair opposite their gaps. */
static void test_pairing_opposite(void) {
    printf("test_pairing_opposite\n");
    static int32_t buf[12000];
    size_t pos = 0;
    const char* code = "11010010001100101101110100101011001010110100101100";
    for(int f = 0; f < 8; f++) {
        for(int i = 0; i < 24; i++) {
            buf[pos++] = 200 + (int32_t)(rng() % 21) - 10;
            buf[pos++] = -(200 + (int32_t)(rng() % 21) - 10);
        }
        buf[pos++] = 850;
        buf[pos++] = -200;
        for(const char* b = code; *b; b++) {
            buf[pos++] = (*b == '1') ? 400 : 200;
            buf[pos++] = (*b == '1') ? -200 : -400;
        }
        buf[pos++] = 200;
        buf[pos++] = (f % 3 == 2) ? -300 : -9000; // every third frame: no gap after it
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingPWM, "PWM");
    CHECK(g_a.encoding_by_pairing && !g_a.params.pwm_equal, "opposite pairs: constant period");
    CHECK(g_a.pair_opposite_pct >= 95, "pairs opposite");
    CHECK(g_a.bit_count == strlen(code) + 1 || g_a.bit_count == strlen(code), "data bits only");
    CHECK(strncmp(g_a.bits, code, strlen(code)) == 0 || strstr(g_a.bits, code) != NULL, "bits");
}

/* Real Manchester data mixes equal and opposite pairs: never overridden. */
static void test_pairing_keeps_manchester(void) {
    printf("test_pairing_keeps_manchester\n");
    static int32_t buf[12000];
    size_t pos = 0;
    for(int f = 0; f < 10; f++) {
        char bits[49];
        for(int i = 0; i < 48; i++)
            bits[i] = (rng() & 1) ? '1' : '0';
        bits[48] = '\0';
        emit_manchester(buf, &pos, bits);
    }
    rg_analyzer_run(buf, pos, &g_a);
    CHECK(g_a.encoding == RgEncodingManchester, "stays Manchester");
    CHECK(!g_a.encoding_by_pairing, "no override");
    CHECK(g_a.pair_same_pct < 93 && g_a.pair_opposite_pct < 93, "pairs mixed");
    CHECK(
        g_a.encoding_fit[RgEncodingManchester] >= g_a.encoding_fit[RgEncodingPWM], "fits listed");
}

static void test_repeat_timing(void) {
    printf("test_repeat_timing\n");
    // Frame A every 24 bits of PWM (24 x 4 Te + stop + 31 Te sync) twice,
    // a long pause (a new press), then twice more.
    static int32_t buf[4000];
    size_t pos = 0;
    const char* a = "101100111000101011110001";
    emit_pwm(buf, &pos, a, 0);
    emit_pwm(buf, &pos, a, 0);
    buf[pos - 1] -= 3000000; // 3 s pause after the second frame
    emit_pwm(buf, &pos, a, 0);
    emit_pwm(buf, &pos, a, 0);
    rg_analyzer_run(buf, pos, &g_a);
    RgRepeatTiming t;
    CHECK(rg_analyzer_repeat_timing(&g_a, 0, &t), "pattern A repeats");
    uint32_t period = (24u * 4u + 1u + 31u) * TE;
    CHECK(t.intervals == 2, "the 3 s pause is a new press, not a repeat");
    CHECK(t.median_us == period && t.min_us == period && t.max_us == period, "interval");
    CHECK(!rg_analyzer_repeat_timing(&g_a, 5, &t) && t.intervals == 0, "no such pattern");
}

static void test_degenerate(void) {
    printf("test_degenerate\n");
    rg_analyzer_run(NULL, 0, &g_a);
    CHECK(g_a.sample_count == 0 && g_a.encoding == RgEncodingUnknown, "empty input is safe");
    CHECK(g_a.bit_count == 0 && g_a.bits[0] == '\0', "no bits from empty input");

    int32_t one[1] = {350};
    rg_analyzer_run(one, 1, &g_a);
    CHECK(g_a.sample_count == 1, "single-sample input handled");
    CHECK(g_a.encoding == RgEncodingUnknown, "one pulse is not an encoding");

    int32_t highs[64];
    for(int i = 0; i < 64; i++)
        highs[i] = 350 + i;
    rg_analyzer_run(highs, 64, &g_a);
    CHECK(g_a.sample_count == 1, "same-level samples merge into one pulse");
    CHECK(g_a.frame_count == 0, "no frames without transitions");

    static int32_t noise[2000];
    size_t pos = 0;
    g_rng = 4242u;
    emit_noise(noise, &pos, 2000);
    rg_analyzer_run(noise, pos, &g_a);
    CHECK(g_a.noise_pct >= 50, "pure noise is mostly outside timing peaks");
    CHECK(g_a.quality == RgQualityPoor, "pure noise graded poor");
    CHECK(g_a.encoding_confidence < 60, "pure noise never gets a confident encoding");
}

int main(void) {
    test_pairing_equal();
    test_pairing_opposite();
    test_pairing_keeps_manchester();
    test_repeat_timing();
    test_pwm_identification();
    test_fixed_code_fields();
    test_changing_fields();
    test_ppm_identification();
    test_manchester_identification();
    test_noise_robustness();
    test_glitch_floor();
    test_glitch_132();
    test_low_first_pwm();
    test_preamble_no_vote();
    test_back_to_back();
    test_gapless_repeats();
    test_repeat_period();
    test_high_separator();
    test_jitter();
    test_truncated_first_frame();
    test_two_buttons();
    test_streaming_equivalence();
    test_align();
    test_decode_api();
    test_similarity();
    test_degenerate();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("ANALYZER TESTS FAILED\n");
        return 1;
    }
    printf("ALL ANALYZER TESTS PASSED\n");
    return 0;
}
