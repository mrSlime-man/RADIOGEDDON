/**
 * Host-side tests for feeding RAW captures to protocol decoders
 * (helpers/rg_decode.c). Build & run via `make -C test check`.
 *
 * The decoder here is a stand-in written for the test (a 24-bit PWM decoder
 * shaped like Princeton's) and the captures are synthetic .sub text built in
 * memory; none of it is the firmware's decoder or a capture from hardware.
 * The firmware's decoders on its own test captures are covered by
 * `make -C test formats` (test_formats.c).
 */
#include "../helpers/rg_decode.h"
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

typedef struct {
    const char* data;
    size_t len;
    size_t pos;
    size_t chunk;
} MemSource;

static size_t mem_read(void* ctx, uint8_t* buf, size_t len) {
    MemSource* m = ctx;
    size_t n = m->len - m->pos;
    if(n > len) n = len;
    if(m->chunk && n > m->chunk) n = m->chunk;
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return n;
}

static bool mem_seek(void* ctx, uint32_t offset) {
    MemSource* m = ctx;
    if(offset > m->len) return false;
    m->pos = offset;
    return true;
}

static RgRawSource mem_source(MemSource* m) {
    RgRawSource s = {mem_read, mem_seek, m};
    return s;
}

/* ---- A recording feed: keeps every pair it is given --------------------- */

typedef struct {
    bool level[64];
    uint32_t duration[64];
    size_t n;
} Recorded;

static void record_feed(void* ctx, bool level, uint32_t duration) {
    Recorded* r = ctx;
    if(r->n < 64) {
        r->level[r->n] = level;
        r->duration[r->n] = duration;
    }
    r->n++;
}

/* ---- Stand-in decoder: 24-bit PWM, te 400 us ----------------------------
 * A bit is a high then a low: 400 + 1200 is 0, 1200 + 400 is 1. After 24 bits
 * a low of at least 10 te ends the frame and it is reported, like a fixed-code
 * remote. Anything else restarts the search. */

typedef struct {
    RgDecodeLog* log;
    uint32_t bits;
    unsigned count;
    bool have_high;
    uint32_t high;
} PwmDecoder;

static bool near(uint32_t v, uint32_t want) {
    return v + 120 >= want && v <= want + 120;
}

static void pwm_reset(PwmDecoder* d) {
    d->count = 0;
    d->bits = 0;
}

static void pwm_feed(void* ctx, bool level, uint32_t duration) {
    PwmDecoder* d = ctx;
    if(level) {
        d->have_high = true;
        d->high = duration;
        return;
    }
    if(!d->have_high) return;
    d->have_high = false;
    bool short_high = near(d->high, 400), long_high = near(d->high, 1200);
    if(duration >= 4000) {
        /* The gap: the last bit's low is part of it, so its high decides. */
        if(d->count == 23 && (short_high || long_high)) {
            uint32_t key = (d->bits << 1) | (long_high ? 1u : 0u);
            char text[64];
            snprintf(text, sizeof(text), "PWM 24bit\r\nKey:0x%06lX\r\n", (unsigned long)key);
            rg_decode_log_add(d->log, "PWM", text, (uint8_t)(key ^ (key >> 8)));
        }
        pwm_reset(d);
        return;
    }
    if(short_high && near(duration, 1200)) {
        d->bits <<= 1;
    } else if(long_high && near(duration, 400)) {
        d->bits = (d->bits << 1) | 1u;
    } else {
        pwm_reset(d);
        return;
    }
    if(++d->count > 23) pwm_reset(d);
}

/* Append one frame of @p key (24 bits) and a 12 ms gap to @p s as RAW text. */
static void append_frame(char* s, size_t cap, uint32_t key) {
    size_t len = strlen(s);
    len += snprintf(s + len, cap - len, "RAW_Data:");
    for(int b = 23; b >= 0; b--) {
        bool one = (key >> b) & 1u;
        len += snprintf(s + len, cap - len, " %d %d", one ? 1200 : 400, one ? -400 : -1200);
    }
    /* The last bit's low merges with the gap, as the radio would record it. */
    len -= strlen(key & 1u ? " -400" : " -1200");
    s[len] = '\0';
    snprintf(s + len, cap - len, " %d\n", (key & 1u) ? -12400 : -13200);
}

/* 24 bits of 1600 us each, before the last low is merged into the gap. */
#define FRAME_US (24u * 1600u)

static void test_level_mapping(void) {
    printf("test_level_mapping\n");
    bool level = false;
    uint32_t duration = 0;
    CHECK(rg_decode_level(350, &level, &duration) && level && duration == 350, "positive is high");
    CHECK(
        rg_decode_level(-1050, &level, &duration) && !level && duration == 1050,
        "negative is low");
    CHECK(!rg_decode_level(0, &level, &duration), "zero is not a sample");
    CHECK(
        rg_decode_level(INT32_MIN, &level, &duration) && !level && duration == 2147483648u,
        "INT32_MIN maps without overflow");
    CHECK(
        rg_decode_level(INT32_MAX, &level, &duration) && level && duration == 2147483647u,
        "INT32_MAX");
}

static void test_feeds_every_sample_in_order(void) {
    printf("test_feeds_every_sample_in_order\n");
    const char* text = "Filetype: Flipper SubGhz RAW File\n"
                       "Version: 1\n"
                       "Protocol: RAW\n"
                       "RAW_Data: 350 -1050 0 1050\r\n"
                       "RAW_Data: -350, 700,\n"
                       "RAW_Data: -9";
    const bool want_level[] = {true, false, true, false, true, false};
    const uint32_t want_dur[] = {350, 1050, 1050, 350, 700, 9};
    for(size_t chunk = 0; chunk <= 7; chunk++) {
        static RgRawReader r;
        MemSource m = {text, strlen(text), 0, chunk};
        rg_raw_reader_init(&r, mem_source(&m));
        RgDecodeLog log;
        rg_decode_log_init(&log);
        Recorded rec = {0};
        uint32_t fed = rg_decode_run(&r, &log, record_feed, &rec, NULL, NULL);
        bool same = rec.n == 6;
        for(size_t i = 0; same && i < 6; i++) {
            same = rec.level[i] == want_level[i] && rec.duration[i] == want_dur[i];
        }
        CHECK(fed == 6 && same, "every sample, in order, sign as level, any read size");
        CHECK(log.samples == 6 && log.time_us == 3509, "samples and time kept current");
        CHECK(log.hit_count == 0 && log.decodes == 0, "nothing decoded by a plain feed");
    }
}

static void test_no_raw_and_corrupt(void) {
    printf("test_no_raw_and_corrupt\n");
    static RgRawReader r;
    RgDecodeLog log;
    Recorded rec = {0};

    const char* key = "Filetype: Flipper SubGhz Key File\nProtocol: Princeton\nKey: 00 11\n";
    MemSource k = {key, strlen(key), 0, 0};
    rg_raw_reader_init(&r, mem_source(&k));
    rg_decode_log_init(&log);
    CHECK(rg_decode_run(&r, &log, record_feed, &rec, NULL, NULL) == 0, "key file feeds nothing");
    CHECK(!r.any_data, "and the reader saw no RAW data");

    const char* bad = "RAW_Data: 100 -2x0 300\nRAW_Data: 400 -500\n";
    MemSource b = {bad, strlen(bad), 0, 0};
    rg_raw_reader_init(&r, mem_source(&b));
    rg_decode_log_init(&log);
    rec.n = 0;
    CHECK(rg_decode_run(&r, &log, record_feed, &rec, NULL, NULL) == 3, "bad line tail skipped");
    CHECK(r.corrupt, "reader marks the file corrupt for the caller to report");
}

static void test_log_dedupe_and_text(void) {
    printf("test_log_dedupe_and_text\n");
    RgDecodeLog log;
    rg_decode_log_init(&log);
    log.time_us = 1000;
    rg_decode_log_add(&log, "Princeton", "Key:0x1\r\nTe:400us\r\n", 7);
    log.time_us = 5000;
    rg_decode_log_add(&log, "Princeton", "Key:0x1\r\nTe:400us\r\n", 7);
    log.time_us = 9000;
    rg_decode_log_add(&log, "Princeton", "Key:0x1\r\nTe:400us\r\n", 7);
    CHECK(log.hit_count == 1 && log.decodes == 3, "repeats are one entry");
    CHECK(log.hit[0].count == 3, "counted three times");
    CHECK(log.hit[0].first_us == 1000 && log.hit[0].last_us == 9000, "first and last time");
    CHECK(strcmp(log.hit[0].text, "Key:0x1\nTe:400us\n") == 0, "carriage returns removed");
    CHECK(strcmp(log.hit[0].protocol, "Princeton") == 0 && !log.hit[0].truncated, "name kept");

    rg_decode_log_add(&log, "Princeton", "Key:0x2\r\nTe:400us\r\n", 7);
    rg_decode_log_add(&log, "Princeton", "Key:0x1\r\nTe:400us\r\n", 8);
    rg_decode_log_add(&log, "CAME", "Key:0x1\r\nTe:400us\r\n", 7);
    CHECK(log.hit_count == 4, "another text, hash or protocol is a new entry");

    rg_decode_log_add(&log, NULL, NULL, 0);
    CHECK(
        log.hit_count == 5 && log.hit[4].protocol[0] == '\0' && log.hit[4].text[0] == '\0',
        "missing name and text are empty");

    char long_text[RG_DECODE_TEXT * 2];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    rg_decode_log_add(&log, "A protocol name longer than the field", long_text, 1);
    RgDecodeHit* h = &log.hit[5];
    CHECK(h->truncated && strlen(h->text) == RG_DECODE_TEXT - 1, "long text cut to fit");
    CHECK(strlen(h->protocol) == RG_DECODE_NAME - 1, "long name cut to fit");
    rg_decode_log_add(&log, "A protocol name longer than the field", long_text, 1);
    CHECK(h->count == 2 && log.hit_count == 6, "a cut text still matches its repeat");

    for(int i = 0; i < 20; i++) {
        char t[16];
        snprintf(t, sizeof(t), "Key:%d", i);
        rg_decode_log_add(&log, "Many", t, (uint8_t)i);
    }
    CHECK(log.hit_count == RG_DECODE_MAX_HITS, "table holds at most RG_DECODE_MAX_HITS");
    CHECK(log.dropped == 6 + 20 - RG_DECODE_MAX_HITS, "new entries beyond it are counted");
    uint32_t before = log.hit[0].count;
    rg_decode_log_add(&log, "Princeton", "Key:0x1\r\nTe:400us\r\n", 7);
    CHECK(log.hit[0].count == before + 1, "a full table still counts known repeats");
    CHECK(log.decodes == 3 + 3 + 1 + 2 + 20 + 1, "every decode counted");
}

static void test_stand_in_decoder(void) {
    printf("test_stand_in_decoder\n");
    static char text[8192];
    strcpy(text, "Filetype: Flipper SubGhz RAW File\nVersion: 1\nProtocol: RAW\n");
    strcat(text, "RAW_Data: 2000 -300 150 -9000\n"); /* noise before the remote */
    for(int i = 0; i < 3; i++)
        append_frame(text, sizeof(text), 0xA5A5A5);
    append_frame(text, sizeof(text), 0x123456);
    append_frame(text, sizeof(text), 0xA5A5A5);

    for(size_t chunk = 0; chunk <= 13; chunk += 13) {
        static RgRawReader r;
        MemSource m = {text, strlen(text), 0, chunk};
        rg_raw_reader_init(&r, mem_source(&m));
        RgDecodeLog log;
        rg_decode_log_init(&log);
        PwmDecoder d = {.log = &log};
        rg_decode_run(&r, &log, pwm_feed, &d, NULL, NULL);

        CHECK(log.decodes == 5, "five frames decoded");
        CHECK(log.hit_count == 2, "two distinct keys");
        CHECK(strcmp(log.hit[0].text, "PWM 24bit\nKey:0xA5A5A5\n") == 0, "first key text");
        CHECK(log.hit[0].count == 4 && log.hit[1].count == 1, "repeat counts");
        CHECK(strcmp(log.hit[1].text, "PWM 24bit\nKey:0x123456\n") == 0, "second key text");

        /* The first frame starts after 11450 us of noise; it is reported on
         * its gap, i.e. at the start of the gap sample: the frame's 24 bits
         * minus its last low, which the gap replaces. */
        uint64_t noise = 2000 + 300 + 150 + 9000;
        uint64_t last_low_a = 400; /* 0xA5A5A5 ends in a 1 bit */
        uint64_t gap_a = 12400, gap_b = 13200;
        uint64_t frame_a = FRAME_US - last_low_a + gap_a;
        uint64_t frame_b = FRAME_US - 1200 + gap_b;
        CHECK(log.hit[0].first_us == noise + frame_a - gap_a, "first decode timed at its gap");
        CHECK(
            log.hit[1].first_us == noise + 3 * frame_a + frame_b - gap_b,
            "second key timed at its gap");
        CHECK(
            log.hit[0].last_us == noise + 4 * frame_a + frame_b - gap_a,
            "last repeat timed at its gap");
        CHECK(log.time_us == noise + 4 * frame_a + frame_b, "whole capture fed");
    }
}

typedef struct {
    uint32_t calls;
    uint32_t last;
    bool increasing;
} ProgressSeen;

static void progress_cb(void* ctx, uint32_t offset) {
    ProgressSeen* p = ctx;
    if(p->calls && offset < p->last) p->increasing = false;
    p->last = offset;
    p->calls++;
}

static void null_feed(void* ctx, bool level, uint32_t duration) {
    (void)ctx;
    (void)level;
    (void)duration;
}

static void test_progress(void) {
    printf("test_progress\n");
    size_t cap = 64 + 5000 * 7;
    char* text = malloc(cap);
    size_t len = (size_t)snprintf(text, cap, "RAW_Data:");
    for(int i = 0; i < 5000; i++) {
        len += (size_t)snprintf(text + len, cap - len, " %d", (i & 1) ? -300 : 300);
    }
    static RgRawReader r;
    MemSource m = {text, len, 0, 0};
    rg_raw_reader_init(&r, mem_source(&m));
    RgDecodeLog log;
    rg_decode_log_init(&log);
    ProgressSeen p = {0, 0, true};
    uint32_t fed = rg_decode_run(&r, &log, null_feed, NULL, progress_cb, &p);
    CHECK(fed == 5000, "all samples fed");
    CHECK(p.calls == 5000 / RG_DECODE_PROGRESS_SAMPLES, "one report per block of samples");
    CHECK(p.increasing && p.last > 0 && p.last <= len, "offsets increase within the file");
    free(text);
}

int main(void) {
    test_level_mapping();
    test_feeds_every_sample_in_order();
    test_no_raw_and_corrupt();
    test_log_dedupe_and_text();
    test_stand_in_decoder();
    test_progress();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("DECODE TESTS FAILED\n");
        return 1;
    }
    printf("ALL DECODE TESTS PASSED\n");
    return 0;
}
