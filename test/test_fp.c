/**
 * Host tests for Signal Fingerprint (helpers/rg_fp.c): fingerprints of
 * synthetic captures built from known bits and timing, analysed by the real
 * analyzer, then compared. Every signal is generated in code, so the
 * expected relation (same structure or not) is exact; none of it is
 * evidence about real captures (those are in test_fwanalyze's benchmark).
 * Run: make -C test
 */
#include "../helpers/rg_fp.h"
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

static uint32_t g_rng = 99u;
static uint32_t rng(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7FFFu;
}
static int32_t jit(int32_t v, int pct) {
    if(!pct) return v;
    return v + (int32_t)((int64_t)v * ((int)(rng() % (2 * pct + 1)) - pct) / 100);
}

#define BUF_MAX 60000
static int32_t g_buf[BUF_MAX];
static size_t g_n;

static void push(int32_t v) {
    if(g_n < BUF_MAX) g_buf[g_n++] = v;
}

/* PWM: 0 = te/3te, 1 = 3te/te, optional preamble of @p pre cells, a stop
 * pulse and a @p sync te gap. */
static void pwm(const char* bits, int te, int pre, int sync, int jitter) {
    for(int i = 0; i < pre; i++)
        push(i % 2 ? -jit(te, jitter) : jit(te, jitter));
    if(pre % 2) push(-jit(4 * te, jitter)); // header gap after the preamble
    for(const char* b = bits; *b; b++) {
        push(jit(*b == '1' ? 3 * te : te, jitter));
        push(-jit(*b == '1' ? te : 3 * te, jitter));
    }
    push(jit(te, jitter));
    push(-jit(sync * te, jitter));
}

/* PPM: fixed pulse, gap 2te = 0, 4te = 1. */
static void ppm(const char* bits, int te, int jitter) {
    for(const char* b = bits; *b; b++) {
        push(jit(te, jitter));
        push(-jit(*b == '1' ? 4 * te : 2 * te, jitter));
    }
    push(jit(te, jitter));
    push(-jit(40 * te, jitter));
}

static void noise(size_t n) {
    for(size_t i = 0; i < n; i++)
        push((i % 2 ? -1 : 1) * (int32_t)(40 + rng() % 900));
}

static RgAnalysis g_an;

static void fingerprint(RgFingerprint* fp, uint32_t hz, uint8_t preset) {
    rg_analyzer_run(g_buf, g_n, &g_an);
    RgFpBuilder b;
    rg_fp_builder_init(&b, &g_an);
    // Fed in uneven chunks, as a file reader would.
    size_t i = 0, step = 7;
    while(i < g_n) {
        size_t k = g_n - i < step ? g_n - i : step;
        rg_fp_builder_feed(&b, g_buf + i, k);
        i += k;
        step = step * 3 % 97 + 1;
    }
    rg_fp_from_analysis(fp, &g_an, &b, hz, preset);
}

static const char* KEY_A = "101100111000101011110001";
static const char* KEY_B = "010011000111010100001110";

static void capture_pwm(RgFingerprint* fp, const char* bits, int te, int reps, int jitter) {
    g_n = 0;
    push(-20000);
    for(int i = 0; i < reps; i++)
        pwm(bits, te, 0, 31, jitter);
    fingerprint(fp, 433920000, 1);
}

static void test_identical(void) {
    printf("test_identical\n");
    RgFingerprint a, b;
    capture_pwm(&a, KEY_A, 350, 6, 0);
    capture_pwm(&b, KEY_A, 350, 6, 0);
    CHECK(a.flags & RG_FP_FLAG_STRUCTURE, "frames decoded");
    CHECK(a.encoding == RgEncodingPWM, "PWM");
    CHECK(a.bit_count == 24, "24 bits");
    CHECK(a.sync_ratio >= 28 && a.sync_ratio <= 34, "sync gap about 31 Te");
    CHECK(a.preamble == 0, "no preamble");
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.score >= 95, "identical captures score at least 95");
    CHECK(m.evidence & RgFpEvPattern, "same frame");
    CHECK(m.evidence & RgFpEvTiming, "same timing");
    CHECK(m.evidence & RgFpEvFrameLength, "same frame length");
    CHECK(m.contradicts == 0, "nothing contradicts");
    CHECK(strcmp(rg_fp_strength(m.score), "Strong") == 0, "strong");
    // Symmetric.
    RgFpMatch r;
    rg_fp_compare(&b, &a, &r);
    CHECK(r.score == m.score && r.evidence == m.evidence, "symmetric");
}

static void test_jitter_offset_repeats(void) {
    printf("test_jitter_offset_repeats\n");
    RgFingerprint a, b, c;
    capture_pwm(&a, KEY_A, 350, 6, 0);
    // 12% jitter, 3 repeats, a burst of noise and half a frame before it.
    g_n = 0;
    noise(40);
    push(-15000);
    for(const char* p = KEY_A + 12; *p; p++) {
        push(*p == '1' ? 1050 : 350);
        push(*p == '1' ? -350 : -1050);
    }
    push(350);
    push(-31 * 350);
    for(int i = 0; i < 3; i++)
        pwm(KEY_A, 350, 0, 31, 12);
    fingerprint(&b, 433920000, 1);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.score >= 85, "jitter, offset and fewer repeats still strong");
    CHECK(m.evidence & RgFpEvTe, "Te agrees");
    CHECK(m.evidence & RgFpEvTiming, "timing agrees despite jitter");
    // Many more repeats.
    capture_pwm(&c, KEY_A, 350, 20, 5);
    rg_fp_compare(&a, &c, &m);
    CHECK(m.score >= 90, "20 repeats vs 6 repeats");
}

static void test_other_button(void) {
    printf("test_other_button\n");
    RgFingerprint a, b;
    capture_pwm(&a, KEY_A, 350, 6, 3);
    capture_pwm(&b, KEY_B, 350, 6, 3);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.score >= 70, "same structure, other bits: similar");
    CHECK(!(m.evidence & RgFpEvPattern), "not the same frame");
    CHECK(m.evidence & RgFpEvFrameLength, "frame length agrees");
}

static void test_different_structures(void) {
    printf("test_different_structures\n");
    RgFingerprint a, b, c, d;
    capture_pwm(&a, KEY_A, 350, 6, 3);
    // Same bits, very different Te.
    capture_pwm(&b, KEY_A, 900, 6, 3);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.score < 85, "Te 350 vs 900 is not strong");
    CHECK(m.contradicts & RgFpEvTe, "Te contradicts");
    // PPM with the same bit count.
    g_n = 0;
    for(int i = 0; i < 6; i++)
        ppm(KEY_A, 400, 3);
    fingerprint(&c, 433920000, 1);
    CHECK(c.encoding == RgEncodingPPM, "PPM decoded");
    rg_fp_compare(&a, &c, &m);
    CHECK(m.score < 70, "PWM vs PPM is not similar");
    CHECK(m.contradicts & RgFpEvTiming || m.contradicts & RgFpEvEncoding, "says why");
    // Same signal on another band.
    g_n = 0;
    push(-20000);
    for(int i = 0; i < 6; i++)
        pwm(KEY_A, 350, 0, 31, 3);
    fingerprint(&d, 315000000, 1);
    rg_fp_compare(&a, &d, &m);
    CHECK(m.contradicts & RgFpEvFrequency, "frequency contradicts");
    CHECK(m.score < 95, "another band lowers the score");
}

static void test_preamble(void) {
    printf("test_preamble\n");
    RgFingerprint a, b;
    g_n = 0;
    for(int i = 0; i < 5; i++)
        pwm(KEY_A, 400, 23, 30, 2);
    fingerprint(&a, 433920000, 1);
    CHECK(a.preamble >= 20 && a.preamble <= 24, "preamble cells measured");
    capture_pwm(&b, KEY_A, 400, 5, 2);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.contradicts & RgFpEvPreamble, "a preamble against none contradicts");
}

static void test_noise(void) {
    printf("test_noise\n");
    RgFingerprint n1, n2, s;
    g_n = 0;
    noise(3000);
    fingerprint(&n1, 433920000, 1);
    g_n = 0;
    noise(3000);
    fingerprint(&n2, 433920000, 1);
    capture_pwm(&s, KEY_A, 350, 6, 3);
    CHECK(!(n1.flags & RG_FP_FLAG_STRUCTURE), "noise has no structure");
    RgFpMatch m;
    rg_fp_compare(&n1, &n2, &m);
    CHECK(m.score <= RG_FP_NO_STRUCTURE_CAP, "noise vs noise capped");
    rg_fp_compare(&n1, &s, &m);
    CHECK(m.score <= RG_FP_NO_STRUCTURE_CAP, "noise vs signal capped");
    // Empty capture.
    g_n = 0;
    RgFingerprint e;
    fingerprint(&e, 0, 0xFF);
    rg_fp_compare(&e, &s, &m);
    CHECK(m.score <= RG_FP_NO_STRUCTURE_CAP, "empty capture capped");
}

static void test_truncated(void) {
    printf("test_truncated\n");
    // A long frame (200 bits) the analyzer reads in part against a full one.
    char longbits[201];
    for(int i = 0; i < 200; i++)
        longbits[i] = (i * 7 + i / 3) % 2 ? '1' : '0';
    longbits[200] = '\0';
    RgFingerprint a, b;
    capture_pwm(&a, longbits, 300, 3, 2);
    g_n = 0;
    push(-20000);
    pwm(longbits, 300, 0, 31, 2);
    // The capture stops in the middle of the second frame.
    size_t keep = g_n;
    pwm(longbits, 300, 0, 31, 2);
    g_n = keep + 150;
    fingerprint(&b, 433920000, 1);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.score >= 70, "a truncated capture still matches its source");
}

static void test_decoded(void) {
    printf("test_decoded\n");
    RgFingerprint a, b, c, r;
    rg_fp_from_decoded(&a, "Princeton", 24, 0x1234u, 433920000, 1);
    rg_fp_from_decoded(&b, "Princeton", 24, 0x9999u, 433920000, 1);
    rg_fp_from_decoded(&c, "CAME", 12, 0x1234u, 433920000, 1);
    RgFpMatch m;
    rg_fp_compare(&a, &b, &m);
    CHECK(m.evidence & RgFpEvProtocol, "same protocol");
    CHECK(m.score >= 85, "same protocol and length");
    rg_fp_compare(&a, &c, &m);
    CHECK(m.contradicts & RgFpEvProtocol, "other protocol contradicts");
    CHECK(m.score < 50, "other protocol is different");
    capture_pwm(&r, KEY_A, 350, 6, 3);
    rg_fp_compare(&a, &r, &m);
    CHECK(m.score <= 60, "RAW vs decoded is capped");
}

static void test_rank(void) {
    printf("test_rank\n");
    RgFpRank r;
    rg_fp_rank_init(&r, 50);
    // 500 candidates with pseudo-random scores: the best 8 are kept, in order.
    uint8_t scores[500];
    for(uint32_t i = 0; i < 500; i++) {
        scores[i] = (uint8_t)(rng() % 101);
        RgFpMatch m = {scores[i], 0, 0};
        rg_fp_rank_offer(&r, i, &m);
    }
    CHECK(r.count == RG_FP_TOP, "eight kept");
    bool ordered = true;
    for(size_t i = 1; i < r.count; i++)
        if(r.c[i].match.score > r.c[i - 1].match.score) ordered = false;
    CHECK(ordered, "best first");
    // The 8th kept is at least as good as every candidate not kept.
    size_t better = 0;
    for(uint32_t i = 0; i < 500; i++)
        if(scores[i] > r.c[r.count - 1].match.score) better++;
    CHECK(better < RG_FP_TOP, "nothing better was dropped");
    for(size_t i = 0; i < r.count; i++)
        CHECK(scores[r.c[i].id] == r.c[i].match.score, "ids match scores");
    RgFpRank low;
    rg_fp_rank_init(&low, 90);
    RgFpMatch m = {89, 0, 0};
    rg_fp_rank_offer(&low, 1, &m);
    CHECK(low.count == 0, "below the minimum is not kept");
}

static void test_serialization(void) {
    printf("test_serialization\n");
    RgFpRecord rec, back;
    memset(&rec, 0, sizeof(rec));
    strcpy(rec.name, "gate_remote.sub");
    rec.size = 12345;
    rec.timestamp = 1700000000u;
    capture_pwm(&rec.fp, KEY_A, 350, 6, 3);
    uint8_t buf[RG_FP_RECORD_BYTES];
    CHECK(rg_fp_record_encode(&rec, buf), "encodes");
    CHECK(rg_fp_record_decode(buf, &back), "decodes");
    CHECK(strcmp(back.name, rec.name) == 0, "name");
    CHECK(back.size == rec.size && back.timestamp == rec.timestamp, "size and time");
    CHECK(memcmp(&back.fp, &rec.fp, sizeof(rec.fp)) == 0, "fingerprint round trip");
    CHECK(rg_fp_record_current(&back, 12345, 1700000000u), "current");
    CHECK(!rg_fp_record_current(&back, 12346, 1700000000u), "changed size is stale");
    CHECK(!rg_fp_record_current(&back, 12345, 1700000001u), "changed time is stale");
    // Every single-bit corruption is caught.
    size_t caught = 0;
    for(size_t i = 0; i < RG_FP_RECORD_BYTES * 8; i++) {
        buf[i / 8] ^= (uint8_t)(1u << (i % 8));
        if(!rg_fp_record_decode(buf, &back)) caught++;
        buf[i / 8] ^= (uint8_t)(1u << (i % 8));
    }
    CHECK(caught == RG_FP_RECORD_BYTES * 8, "every bit flip is detected");
    // Names that cannot be stored.
    RgFpRecord bad = rec;
    bad.name[0] = '\0';
    CHECK(!rg_fp_record_encode(&bad, buf), "empty name refused");
    memset(bad.name, 'a', sizeof(bad.name));
    CHECK(!rg_fp_record_encode(&bad, buf), "unterminated name refused");
    strcpy(bad.name, "a/b.sub");
    CHECK(!rg_fp_record_encode(&bad, buf), "path refused");
    // Header.
    uint8_t h[RG_FP_HEADER_BYTES];
    rg_fp_header_encode(42, h);
    CHECK(rg_fp_header_decode(h) == 42, "header count");
    h[4] ^= 1;
    CHECK(rg_fp_header_decode(h) < 0, "other version refused");
    rg_fp_header_encode(RG_FP_INDEX_MAX + 1, h);
    CHECK(rg_fp_header_decode(h) < 0, "oversized count refused");
    memset(h, 0, sizeof(h));
    CHECK(rg_fp_header_decode(h) < 0, "zeroed header refused");
    // A decoded fingerprint with impossible values is refused.
    uint8_t raw[RG_FP_BYTES];
    rg_fp_encode(&rec.fp, raw);
    raw[2] = RgEncodingCount;
    RgFingerprint f;
    CHECK(!rg_fp_decode(raw, &f), "bad encoding refused");
}

static void test_evidence_text(void) {
    printf("test_evidence_text\n");
    char t[48];
    rg_fp_evidence_text(RgFpEvTe | RgFpEvPattern | RgFpEvFrequency, 2, t, sizeof(t));
    CHECK(strcmp(t, "same frame, Te") == 0, "most telling first, at most two");
    rg_fp_evidence_text(0, 3, t, sizeof(t));
    CHECK(t[0] == '\0', "none");
    char small[8];
    rg_fp_evidence_text(0xFFF, 12, small, sizeof(small));
    CHECK(strlen(small) < sizeof(small), "fits a small buffer");
    CHECK(strcmp(rg_fp_evidence_name(RgFpEvSync), "sync gap") == 0, "name");
    CHECK(
        rg_fp_bin(350, 350) == 1 && rg_fp_bin(1050, 350) == 4 && rg_fp_bin(100, 350) == 0,
        "buckets");
    CHECK(
        rg_fp_bin(1000000, 350) == RG_FP_BINS - 1 && rg_fp_bin(5, 0) == RG_FP_BINS - 1,
        "open bucket and no Te");
}

/* The builder fed during the analyzer's second pass (as Find Similar does
 * since beta 11, radiogeddon_analysis_run_raw's tap) ends exactly as when
 * it is fed in a pass of its own after the analysis. */
static void test_builder_in_pass_two(void) {
    printf("test_builder_in_pass_two\n");
    static const int tes[] = {300, 450, 620};
    for(int k = 0; k < 4; k++) {
        g_n = 0;
        push(-20000);
        if(k == 3) noise(300);
        for(int i = 0; i < 6; i++)
            pwm(k % 2 ? KEY_A : KEY_B, tes[k % 3], i == 0 ? 12 : 0, 31, 5);
        // Separate pass after the analysis (beta 10).
        rg_analyzer_run(g_buf, g_n, &g_an);
        RgFpBuilder after;
        rg_fp_builder_init(&after, &g_an);
        rg_fp_builder_feed(&after, g_buf, g_n);
        // Along with pass 2, in chunks of 64 like the file runner.
        static RgAnalyzer an;
        static RgAnalysis res;
        RgFpBuilder along;
        rg_fp_builder_init(&along, NULL);
        rg_analyzer_begin(&an, &res);
        do {
            bool tapped = an.pass == 2;
            if(tapped) rg_fp_builder_init(&along, &res);
            for(size_t i = 0; i < g_n; i += 64) {
                size_t n = g_n - i < 64 ? g_n - i : 64;
                rg_analyzer_feed(&an, g_buf + i, n);
                if(tapped) rg_fp_builder_feed(&along, g_buf + i, n);
            }
        } while(rg_analyzer_next_pass(&an));
        CHECK(memcmp(&after, &along, sizeof(after)) == 0, "same builder state");
        RgFingerprint fa, fb;
        rg_analyzer_hypotheses(&an);
        rg_fp_from_analysis(&fa, &g_an, &after, 433920000, 1);
        rg_fp_from_analysis(&fb, &res, &along, 433920000, 1);
        CHECK(memcmp(&fa, &fb, sizeof(fa)) == 0, "same fingerprint");
    }
}

int main(void) {
    test_builder_in_pass_two();
    test_identical();
    test_jitter_offset_repeats();
    test_other_button();
    test_different_structures();
    test_preamble();
    test_noise();
    test_truncated();
    test_decoded();
    test_rank();
    test_serialization();
    test_evidence_text();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
