/**
 * Host-side tests for the streaming RAW_Data reader (helpers/rg_raw.c) and
 * its use as the analyzer's input. Build & run via `make -C test check`.
 *
 * Inputs are synthetic .sub text built in memory, plus the repository's
 * test/fixtures/raw_ref.sub; none of it is a capture from real hardware.
 */
#include "../helpers/rg_raw.h"
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

/* In-memory byte source; `chunk` caps each read to exercise buffer edges. */
typedef struct {
    const char* data;
    size_t len;
    size_t pos;
    size_t chunk;
    unsigned seeks;
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
    m->seeks++;
    return true;
}

static RgRawSource mem_source(MemSource* m) {
    RgRawSource s = {mem_read, mem_seek, m};
    return s;
}

static size_t read_all(RgRawReader* r, int32_t* out, size_t cap, size_t step) {
    size_t n = 0;
    while(n < cap) {
        size_t want = cap - n < step ? cap - n : step;
        size_t got = rg_raw_reader_read(r, out + n, want);
        if(got == 0) break;
        n += got;
    }
    return n;
}

static const char* k_small = "Filetype: Flipper SubGhz RAW File\n"
                             "Version: 1\n"
                             "Frequency: 433920000\n"
                             "Protocol: RAW\n"
                             "RAW_Data: 350 -1050 0 1050\r\n"
                             "RAW_Data: -350 2147483647 -12000\n"
                             "RAW_Data: 7 -8"; /* no trailing newline */

static void test_basic_parse(void) {
    printf("test_basic_parse\n");
    static RgRawReader r;
    int32_t out[16];
    const int32_t expect[] = {350, -1050, 1050, -350, 2147483647, -12000, 7, -8};
    for(size_t chunk = 0; chunk <= 5; chunk += 5) {
        MemSource m = {k_small, strlen(k_small), 0, chunk, 0};
        rg_raw_reader_init(&r, mem_source(&m));
        size_t n = read_all(&r, out, 16, chunk ? 1 : 16);
        CHECK(n == 8, "eight non-zero values (zero skipped)");
        CHECK(memcmp(out, expect, sizeof(expect)) == 0, "values, signs and int32 max parsed");
        CHECK(r.any_data && !r.corrupt, "RAW data seen, nothing corrupt");
        CHECK(r.index == 8, "sample index counts values");
        CHECK(
            r.time_us == 350ull + 1050 + 1050 + 350 + 2147483647ull + 12000 + 7 + 8, "time sums");
        CHECK(rg_raw_reader_read(&r, out, 16) == 0, "end of data stays at end");
    }
}

static void test_corrupt_and_non_raw(void) {
    printf("test_corrupt_and_non_raw\n");
    static RgRawReader r;
    int32_t out[16];
    const char* bad = "RAW_Data: 100 -2x0 300\n"
                      "RAW_Data: 400 - 500\n"
                      "RAW_Data: -600\n";
    MemSource m = {bad, strlen(bad), 0, 0, 0};
    rg_raw_reader_init(&r, mem_source(&m));
    size_t n = read_all(&r, out, 16, 16);
    CHECK(r.corrupt, "garbage token flags the file corrupt");
    CHECK(n == 3 && out[0] == 100 && out[1] == 400 && out[2] == -600, "bad line tail skipped");

    const char* key = "Filetype: Flipper SubGhz Key File\nProtocol: Princeton\nKey: 00 11\n";
    MemSource k = {key, strlen(key), 0, 0, 0};
    rg_raw_reader_init(&r, mem_source(&k));
    CHECK(rg_raw_reader_read(&r, out, 16) == 0, "key file yields no samples");
    CHECK(!r.any_data && !r.corrupt, "non-RAW file is not corrupt, just empty");

    const char* nearly = "RAW_Dat: 5 6\nXRAW_Data: 7\n";
    MemSource q = {nearly, strlen(nearly), 0, 0, 0};
    rg_raw_reader_init(&r, mem_source(&q));
    CHECK(rg_raw_reader_read(&r, out, 16) == 0, "only exact RAW_Data lines are read");
}

/* A long synthetic recording: 6000 values over 512-value lines plus one
 * 3000-value line, as a recorder might write. */
static char* build_big(size_t* len, int32_t* values, size_t count) {
    size_t cap = count * 8 + 256;
    char* text = malloc(cap);
    size_t p = (size_t)snprintf(text, cap, "Filetype: Flipper SubGhz RAW File\nProtocol: RAW\n");
    size_t on_line = 0, line_cap = 512;
    for(size_t i = 0; i < count; i++) {
        int32_t v = (int32_t)(100 + (i * 37) % 900);
        values[i] = (i % 2) ? -v : v;
        if(on_line == 0) p += (size_t)snprintf(text + p, cap - p, "RAW_Data:");
        p += (size_t)snprintf(text + p, cap - p, " %d", (int)values[i]);
        if(++on_line == line_cap) {
            text[p++] = '\n';
            on_line = 0;
            line_cap = (i > 2000 && i < 3000) ? 3000 : 512;
        }
    }
    text[p++] = '\n';
    text[p] = '\0';
    *len = p;
    return text;
}

static void test_checkpoints_and_seek(void) {
    printf("test_checkpoints_and_seek\n");
    enum {
        COUNT = 6000
    };
    static int32_t values[COUNT], out[COUNT];
    static RgRawReader r;
    size_t len = 0;
    char* text = build_big(&len, values, COUNT);
    MemSource m = {text, len, 0, 13, 0};
    rg_raw_reader_init(&r, mem_source(&m));
    size_t n = read_all(&r, out, COUNT, 100);
    CHECK(n == COUNT && memcmp(out, values, sizeof(values)) == 0, "whole file read in chunks");
    CHECK(r.cp_count > 4 && r.cp_count <= RG_RAW_CHECKPOINTS, "bounded checkpoint table");
    bool ordered = true;
    for(size_t i = 1; i < r.cp_count; i++)
        if(r.cp[i].index <= r.cp[i - 1].index) ordered = false;
    CHECK(ordered, "checkpoints strictly increasing");

    uint64_t total = r.time_us;
    const uint64_t targets[] = {0, 1, total / 3, total / 2, total - 1, total + 5};
    bool all_ok = true;
    for(size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
        rg_raw_reader_seek_time(&r, targets[t]);
        if(r.time_us > targets[t]) all_ok = false;
        uint32_t at = r.index;
        int32_t win[64];
        size_t got = rg_raw_reader_read(&r, win, 64);
        for(size_t i = 0; i < got; i++)
            if(at + i >= COUNT || win[i] != values[at + i]) all_ok = false;
        if(targets[t] >= total / 3 && at < 1000) all_ok = false; /* seek did skip ahead */
    }
    CHECK(all_ok, "seek lands at or before the target and resumes with the right samples");

    rg_raw_reader_rewind(&r);
    CHECK(r.index == 0 && r.time_us == 0, "rewind resets position");
    n = read_all(&r, out, COUNT, 999);
    CHECK(n == COUNT && memcmp(out, values, sizeof(values)) == 0, "re-read after seeks is exact");
    free(text);
}

static void test_fixture_end_to_end(void) {
    printf("test_fixture_end_to_end\n");
    FILE* f = fopen("fixtures/raw_ref.sub", "rb");
    CHECK(f != NULL, "fixture opened (run from test/)");
    if(!f) return;
    static char text[4096];
    size_t len = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[len] = '\0';

    MemSource m = {text, len, 0, 0, 0};
    static RgRawReader r;
    static RgAnalyzer a;
    rg_raw_reader_init(&r, mem_source(&m));
    rg_analyzer_begin(&a);
    int32_t chunk[32];
    do {
        rg_raw_reader_rewind(&r);
        size_t n;
        while((n = rg_raw_reader_read(&r, chunk, 32)) > 0)
            rg_analyzer_feed(&a, chunk, n);
    } while(rg_analyzer_next_pass(&a));
    CHECK(r.index == 52, "52 samples in the fixture");
    CHECK(a.result.encoding == RgEncodingPWM, "fixture reads as PWM");
    CHECK(a.result.te_us == 400, "fixture Te 400us");
    CHECK(a.result.frame_count == 2 && a.result.groups[0].exact == 2, "two identical frames");
    CHECK(strcmp(a.result.bits, "001011000101") == 0, "fixture bits");
}

int main(void) {
    test_basic_parse();
    test_corrupt_and_non_raw();
    test_checkpoints_and_seek();
    test_fixture_end_to_end();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("RAW READER TESTS FAILED\n");
        return 1;
    }
    printf("ALL RAW READER TESTS PASSED\n");
    return 0;
}
