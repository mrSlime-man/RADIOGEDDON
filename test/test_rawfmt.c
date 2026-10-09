/**
 * Host-side tests for the streaming RAW .sub formatter (helpers/rg_rawfmt.c):
 * exact firmware layout, line splitting, small output buffers, the lost-sample
 * trailer, and a round trip through the RAW reader (helpers/rg_raw.c).
 * Build & run via `make -C test check`. All samples are synthetic.
 */
#include "../helpers/rg_rawfmt.h"
#include "../helpers/rg_raw.h"
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

/* Format all values through an output buffer of @p cap bytes, appending each
 * filled buffer to @p dst, as the writer thread does. */
static size_t
    format_all(RgRawFmt* f, const int32_t* v, size_t n, size_t cap, char* dst, size_t dst_cap) {
    char* buf = malloc(cap);
    size_t total = 0, done = 0, len = 0;
    while(done < n) {
        done += rg_rawfmt_values(f, v + done, n - done, buf, cap, &len);
        if(cap - len < RG_RAWFMT_VALUE_MAX || done == n) {
            if(total + len < dst_cap) memcpy(dst + total, buf, len);
            total += len;
            len = 0;
        }
    }
    if(rg_rawfmt_end_line(f, buf, cap, &len)) {
        if(total + len < dst_cap) memcpy(dst + total, buf, len);
        total += len;
    }
    free(buf);
    if(total < dst_cap) dst[total] = '\0';
    return total;
}

static void test_header(void) {
    printf("test_header\n");
    char out[256];
    size_t n = rg_rawfmt_header(out, sizeof(out), 433920000, "FuriHalSubGhzPresetOok650Async");
    const char* expect = "Filetype: Flipper SubGhz RAW File\n"
                         "Version: 1\n"
                         "Frequency: 433920000\n"
                         "Preset: FuriHalSubGhzPresetOok650Async\n"
                         "Protocol: RAW\n";
    CHECK(n == strlen(expect) && strcmp(out, expect) == 0, "header matches the firmware layout");
    CHECK(rg_rawfmt_header(out, 20, 433920000, "x") == 0, "too small a buffer: refused");
}

static void test_values_and_lines(void) {
    printf("test_values_and_lines\n");
    RgRawFmt f;
    char out[256];
    size_t len = 0;
    rg_rawfmt_init(&f, RG_RAWFMT_LINE_VALUES);
    const int32_t v[] = {400, -1200, 0, 500};
    CHECK(rg_rawfmt_values(&f, v, 4, out, sizeof(out), &len) == 4, "all consumed");
    CHECK(rg_rawfmt_end_line(&f, out, sizeof(out), &len), "line closed");
    out[len] = '\0';
    CHECK(strcmp(out, "RAW_Data: 400 -1200 500\n") == 0, "single spaces, zero skipped");
    CHECK(
        rg_rawfmt_end_line(&f, out, sizeof(out), &len) && out[len - 1] == '\n',
        "closing an already closed line adds nothing");

    rg_rawfmt_init(&f, 3);
    const int32_t w[] = {1, -2, 3, -4, 5, -6, 7};
    char all[256];
    format_all(&f, w, 7, 128, all, sizeof(all));
    CHECK(
        strcmp(all, "RAW_Data: 1 -2 3\nRAW_Data: -4 5 -6\nRAW_Data: 7\n") == 0,
        "lines split after per_line values");

    rg_rawfmt_init(&f, 2);
    const int32_t ext[] = {-2147483647 - 1, 2147483647};
    format_all(&f, ext, 2, 64, all, sizeof(all));
    CHECK(strcmp(all, "RAW_Data: -2147483648 2147483647\n") == 0, "int32 extremes");

    rg_rawfmt_init(&f, 512);
    len = 0;
    CHECK(
        rg_rawfmt_values(&f, w, 7, out, RG_RAWFMT_VALUE_MAX - 1, &len) == 0 && len == 0,
        "no room for one value: nothing consumed");
}

/* Same text whatever the output buffer size. */
static void test_buffer_sizes(void) {
    printf("test_buffer_sizes\n");
    enum {
        N = 3000
    };
    static int32_t v[N];
    uint32_t seed = 12345;
    for(size_t i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        int32_t mag = (int32_t)(1 + (seed >> 8) % 3000000u);
        v[i] = i % 2 ? -mag : mag;
    }
    static char ref[64 * 1024], got[64 * 1024];
    RgRawFmt f;
    rg_rawfmt_init(&f, RG_RAWFMT_LINE_VALUES);
    size_t ref_len = format_all(&f, v, N, 4096, ref, sizeof(ref));
    bool same = true;
    const size_t caps[] = {RG_RAWFMT_VALUE_MAX, 25, 37, 64, 511, 2048};
    for(size_t c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
        rg_rawfmt_init(&f, RG_RAWFMT_LINE_VALUES);
        size_t n = format_all(&f, v, N, caps[c], got, sizeof(got));
        same = same && n == ref_len && memcmp(ref, got, n) == 0;
    }
    CHECK(same, "identical output for buffers from 24 bytes to 2 KB");

    // Lines: at most 512 values, each starting with the key, no double or
    // trailing spaces (the firmware file encoder splits on single spaces).
    size_t lines = 0, max_vals = 0;
    bool clean = true;
    for(const char* p = ref; *p;) {
        const char* end = strchr(p, '\n');
        if(!end) {
            clean = false;
            break;
        }
        clean = clean && strncmp(p, "RAW_Data: ", 10) == 0;
        size_t vals = 0;
        for(const char* q = p; q < end; q++) {
            if(*q == ' ') {
                vals++;
                clean = clean && q[1] != ' ' && q + 1 != end;
            }
        }
        if(vals > max_vals) max_vals = vals;
        lines++;
        p = end + 1;
    }
    CHECK(clean, "every line is a clean RAW_Data line");
    CHECK(lines == (N + 511) / 512 && max_vals == 512, "512 values a line, the rest last");
}

typedef struct {
    const char* data;
    size_t len;
    size_t pos;
} Src;

static size_t src_read(void* ctx, uint8_t* buf, size_t len) {
    Src* s = ctx;
    size_t n = s->len - s->pos;
    if(n > len) n = len;
    memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    return n;
}

static bool src_seek(void* ctx, uint32_t offset) {
    Src* s = ctx;
    if(offset > s->len) return false;
    s->pos = offset;
    return true;
}

static void test_round_trip(void) {
    printf("test_round_trip\n");
    enum {
        N = 5000
    };
    static int32_t v[N], back[N + 8];
    uint32_t seed = 777;
    for(size_t i = 0; i < N; i++) {
        seed = seed * 1664525u + 1013904223u;
        int32_t mag = (int32_t)(1 + (seed >> 9) % 20000u);
        v[i] = i % 2 ? -mag : mag;
    }
    static char file[128 * 1024];
    size_t len = rg_rawfmt_header(file, sizeof(file), 315000000, "FuriHalSubGhzPresetOok270Async");
    RgRawFmt f;
    rg_rawfmt_init(&f, RG_RAWFMT_LINE_VALUES);
    len += format_all(&f, v, N, 1000, file + len, sizeof(file) - len);
    size_t t = rg_rawfmt_lost(file + len, sizeof(file) - len, 42, 3, 1777);
    CHECK(
        t > 0 &&
            strncmp(file + len, "# Lost: 42 samples in 3 gaps, first after sample 1777", 53) == 0,
        "lost trailer text");
    len += t;

    Src s = {file, len, 0};
    RgRawSource src = {src_read, src_seek, &s};
    static RgRawReader r;
    rg_raw_reader_init(&r, src);
    size_t n = 0, got;
    while((got = rg_raw_reader_read(&r, back + n, 64)) > 0)
        n += got;
    CHECK(n == N && memcmp(v, back, sizeof(v)) == 0, "reader returns exactly what was written");
    CHECK(!r.corrupt && r.any_data, "clean RAW file");
    CHECK(r.lost == 42, "reader picks up the lost count");

    char small[8];
    CHECK(rg_rawfmt_lost(small, sizeof(small), 5, 1, 0) == 0, "trailer refused when too small");
    CHECK(rg_rawfmt_lost(file, sizeof(file), 0, 0, 0) == 0, "no trailer without losses");
}

int main(void) {
    test_header();
    test_values_and_lines();
    test_buffer_sizes();
    test_round_trip();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("RAW FORMAT TESTS FAILED\n");
        return 1;
    }
    printf("ALL RAW FORMAT TESTS PASSED\n");
    return 0;
}
