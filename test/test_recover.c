/* Host tests for rg_recover: recordings cut off by a reboot.
 *
 * A recording is written exactly as the recorder writes it (rg_rawfmt, short
 * lines so a small file has many), then cut at every byte offset. Each cut is
 * scanned, repaired as the app repairs it (first `cut` bytes + rg_recover_tail)
 * and read back through rg_raw, which must return exactly the values whose
 * text and following separator reached the card: none invented, none
 * shortened, none lost before the last complete one. The same is repeated with
 * a tail of zero bytes and of 0xFF (clusters allocated but never written), and
 * the repair must be idempotent (a repaired file scans as complete). */
#include "helpers/rg_recover.h"
#include "helpers/rg_rawfmt.h"
#include "helpers/rg_raw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c)                                                  \
    do {                                                          \
        checks++;                                                 \
        if(!(c)) {                                                \
            failures++;                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                         \
    } while(0)

#define NVALUES 150
#define MAXTEXT 8192

static int32_t g_values[NVALUES];
static char g_text[MAXTEXT];
static size_t g_len;
/* g_end[i]: offset of the byte after value i's separator (space or '\n'). */
static size_t g_end[NVALUES];

static void build_recording(bool lost_note) {
    srand(7);
    RgRawFmt f;
    rg_rawfmt_init(&f, 8);
    g_len = rg_rawfmt_header(g_text, MAXTEXT, 433920000, "FuriHalSubGhzPresetOok650Async");
    for(int i = 0; i < NVALUES; i++) {
        int32_t mag = 100 + rand() % 30000;
        g_values[i] = (i % 2) ? -mag : mag;
        rg_rawfmt_values(&f, &g_values[i], 1, g_text, MAXTEXT, &g_len);
        // Complete once the byte after its digits (the next value's space,
        // or the '\n' ending the line) is on the card.
        g_end[i] = g_text[g_len - 1] == '\n' ? g_len : g_len + 1;
    }
    rg_rawfmt_end_line(&f, g_text, MAXTEXT, &g_len);
    if(lost_note) g_len += rg_rawfmt_lost(g_text + g_len, MAXTEXT - g_len, 12, 1, 99);
}

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
} MemSrc;

static size_t mem_read(void* ctx, uint8_t* buf, size_t len) {
    MemSrc* m = ctx;
    size_t n = m->len - m->pos < len ? m->len - m->pos : len;
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return n;
}

static bool mem_seek(void* ctx, uint32_t offset) {
    MemSrc* m = ctx;
    if(offset > m->len) return false;
    m->pos = offset;
    return true;
}

/* rg_raw over @p text: number of samples, and whether they match g_values. */
static size_t read_back(const char* text, size_t len, bool* match, bool* corrupt) {
    MemSrc m = {(const uint8_t*)text, len, 0};
    static RgRawReader r;
    rg_raw_reader_init(&r, (RgRawSource){mem_read, mem_seek, &m});
    int32_t buf[32];
    size_t total = 0, n;
    *match = true;
    while((n = rg_raw_reader_read(&r, buf, 32)) > 0) {
        for(size_t i = 0; i < n; i++, total++)
            if(total >= NVALUES || buf[i] != g_values[total]) *match = false;
    }
    *corrupt = r.corrupt;
    return total;
}

/* Scan @p len bytes (+ @p tail_len bytes of @p tail_byte), repair, read back. */
static int check_cut(size_t len, int tail_byte, size_t tail_len, int* complete_seen) {
    static uint8_t file[MAXTEXT + 256];
    memcpy(file, g_text, len);
    for(size_t i = 0; i < tail_len; i++)
        file[len + i] = (uint8_t)tail_byte;
    RgRecoverScan s;
    rg_recover_init(&s);
    // Fed in uneven chunks, as the app reads.
    for(size_t off = 0; off < len + tail_len;) {
        size_t k = 37;
        if(off + k > len + tail_len) k = len + tail_len - off;
        rg_recover_feed(&s, file + off, k);
        off += k;
    }
    RgRecoverVerdict v = rg_recover_finish(&s);
    size_t expect = 0;
    while(expect < NVALUES && g_end[expect] <= len)
        expect++;
    int bad = 0;
    if(s.samples != expect) bad |= 1;
    if(expect == 0 && v != RgRecoverNothing && v != RgRecoverEmpty) bad |= 2;
    if(expect > 0 && v != RgRecoverPartial && v != RgRecoverComplete) bad |= 4;
    if(v == RgRecoverComplete) (*complete_seen)++;
    if(expect > 0) {
        static char repaired[MAXTEXT + 256];
        memcpy(repaired, file, s.cut);
        size_t t = rg_recover_tail(&s, repaired + s.cut, sizeof(repaired) - s.cut);
        if(v == RgRecoverPartial && t == 0) bad |= 8;
        size_t rlen = s.cut + (v == RgRecoverPartial ? t : 0);
        bool match, corrupt;
        size_t got = read_back(repaired, rlen, &match, &corrupt);
        if(got != expect || !match || corrupt) bad |= 16;
        // Idempotent: the repaired file scans as complete with the same samples.
        RgRecoverScan again;
        rg_recover_init(&again);
        rg_recover_feed(&again, (const uint8_t*)repaired, rlen);
        if(rg_recover_finish(&again) != RgRecoverComplete || again.samples != expect ||
           again.frequency != 433920000)
            bad |= 32;
    }
    return bad;
}

static void test_every_cut(int tail_byte, size_t tail_len, const char* label) {
    printf("test_every_cut (%s)\n", label);
    build_recording(false);
    int bad = 0, complete = 0;
    size_t first_bad = 0;
    for(size_t len = 0; len <= g_len; len++) {
        int b = check_cut(len, tail_byte, tail_len, &complete);
        if(b && !bad) first_bad = len;
        bad |= b;
    }
    printf("  %zu cut points, %d values each at most\n", g_len + 1, NVALUES);
    if(bad) printf("  first failure at %zu (flags %d)\n", first_bad, bad);
    CHECK(bad == 0);
    // Without the recorder's closing note no cut is "complete".
    CHECK(complete == 0);
}

static void test_finished_file(void) {
    printf("test_finished_file\n");
    build_recording(true); // with a "# Lost:" note: the recorder closed it
    RgRecoverScan s;
    rg_recover_init(&s);
    rg_recover_feed(&s, (const uint8_t*)g_text, g_len);
    CHECK(rg_recover_finish(&s) == RgRecoverComplete);
    CHECK(s.samples == NVALUES && s.cut == g_len);
    CHECK(s.frequency == 433920000);
    uint64_t total = 0;
    for(int i = 0; i < NVALUES; i++)
        total += (uint64_t)(g_values[i] < 0 ? -g_values[i] : g_values[i]);
    CHECK(s.duration_us == total);
}

static void test_not_raw(void) {
    printf("test_not_raw\n");
    const char* key =
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: Princeton\nBit: 24\nKey: 00 00 00 00 00 12 34 56\n";
    RgRecoverScan s;
    rg_recover_init(&s);
    rg_recover_feed(&s, (const uint8_t*)key, strlen(key));
    CHECK(rg_recover_finish(&s) == RgRecoverNothing);
    rg_recover_init(&s);
    CHECK(rg_recover_finish(&s) == RgRecoverNothing); // an empty file
    const char* header_only =
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 315000000\n"
        "Preset: FuriHalSubGhzPresetOok270Async\nProtocol: RAW\nRAW_Data: 1";
    rg_recover_init(&s);
    rg_recover_feed(&s, (const uint8_t*)header_only, strlen(header_only));
    CHECK(rg_recover_finish(&s) == RgRecoverEmpty);
    CHECK(s.frequency == 315000000);
    // Overlong numbers and stray bytes stop the scan where they start.
    const char* junk = "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n"
                       "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: 400 -800 "
                       "99999999999 -400\n";
    rg_recover_init(&s);
    rg_recover_feed(&s, (const uint8_t*)junk, strlen(junk));
    CHECK(rg_recover_finish(&s) == RgRecoverPartial);
    CHECK(s.samples == 2);
    const char* crlf =
        "Filetype: Flipper SubGhz RAW File\r\nVersion: 1\r\nFrequency: 433920000\r\n"
        "Preset: FuriHalSubGhzPresetOok650Async\r\nProtocol: RAW\r\nRAW_Data: 400 -800\r\n";
    rg_recover_init(&s);
    rg_recover_feed(&s, (const uint8_t*)crlf, strlen(crlf));
    CHECK(rg_recover_finish(&s) == RgRecoverPartial && s.samples == 2);
}

int main(void) {
    test_every_cut(0, 0, "plain cut");
    test_every_cut(0x00, 200, "zero-filled tail");
    test_every_cut(0xFF, 200, "0xFF tail");
    test_finished_file();
    test_not_raw();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
