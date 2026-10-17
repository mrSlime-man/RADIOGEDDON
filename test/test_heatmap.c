/* Host tests for rg_heatmap: the RF activity heatmap read from logs written
 * by rg_actlog itself (the logger's own rows, header and footer), fed in
 * uneven chunks and split into parts as the device writes them.
 *
 * Checked: the frequencies of the first interval, sorted for display; a
 * frequency nobody listened to is "not observed", never quiet; quiet and
 * active cells; burst totals kept through column merging; coverage; parts
 * read one after another give the same map as one file; malformed and
 * foreign lines skipped; the footer seen. All observations are synthetic. */
#include "helpers/rg_heatmap.h"
#include "helpers/rg_actlog.h"

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

#define LOG_MAX (256 * 1024)
static char g_log[LOG_MAX];
static size_t g_len;
static size_t g_part2; /* offset where a second part would start */

static const uint32_t FREQS[4] = {868350000, 315000000, 433920000, 390000000};
static uint32_t g_events[4];

static void append(const char* s, size_t n) {
    if(g_len + n < LOG_MAX) {
        memcpy(g_log + g_len, s, n);
        g_len += n;
    }
}

/* @p intervals of 10 s. Channel 0 (868.35) is busy every 7th interval,
 * channel 1 (315) quiet, channel 2 (433.92) busy every 3rd, channel 3
 * (390) skipped by the schedule in every other interval. */
static void build_log(int intervals) {
    static RgActlog log;
    rg_actlog_init(&log, FREQS, 4, 1u << 30, 0);
    g_len = 0;
    memset(g_events, 0, sizeof(g_events));
    char line[512];
    append(
        line,
        rg_actlog_header(&log, "AM650, dwell 20 ms", "2026-10-16 10:00", line, sizeof(line)));
    uint32_t now = 0;
    for(int k = 0; k < intervals; k++) {
        for(int v = 0; v < 50; v++) { // visits within the interval
            now += 200;
            int ch = v % 4;
            if(ch == 3 && (k % 2)) continue; // not listened to
            bool active = (ch == 0 && k % 7 == 0) || (ch == 2 && k % 3 == 0);
            bool started = active && v < 4;
            if(started) g_events[ch]++;
            rg_actlog_observe(&log, (size_t)ch, active, started, active ? -50 : -95, -96, 20, now);
        }
        rg_actlog_tick(&log, now);
        for(size_t i = 0; i < 4; i++)
            append(line, rg_actlog_row(&log, i, now, line, sizeof(line)));
        rg_actlog_end_interval(&log, now);
        if(k == intervals / 2) {
            g_part2 = g_len;
            // The next part starts with its own header, as on the card.
            append(line, rg_actlog_header(&log, "AM650", "2026-10-16 10:00", line, sizeof(line)));
        }
    }
    append(line, rg_actlog_footer(&log, "duration reached", line, sizeof(line)));
}

static RgHeatCell* g_cells;

static void map_log(RgHeatmap* h, const char* data, size_t len, size_t chunk) {
    rg_heatmap_probe_begin(h);
    for(size_t off = 0; off < len && !rg_heatmap_probe_done(h); off += chunk)
        rg_heatmap_feed(h, data + off, len - off < chunk ? len - off : chunk);
    rg_heatmap_end(h);
    free(g_cells);
    g_cells = malloc(rg_heatmap_cells_bytes(h) + 1);
    rg_heatmap_begin(h, g_cells);
    for(size_t off = 0; off < len; off += chunk)
        rg_heatmap_feed(h, data + off, len - off < chunk ? len - off : chunk);
    rg_heatmap_end(h);
}

static void test_states(void) {
    printf("test_states\n");
    build_log(12); // 120 s: one column per interval
    static RgHeatmap h;
    map_log(&h, g_log, g_len, 37);
    CHECK(h.channels == 4);
    CHECK(rg_heatmap_row_freq(&h, 0) == 315000000 && rg_heatmap_row_freq(&h, 3) == 868350000);
    CHECK(h.span_s == 10 && h.cols_used == 12 && h.end_s == 120);
    CHECK(h.ended && h.skipped == 0 && h.rows == 48);
    // Row 1 is 390 MHz: listened to only in even intervals.
    for(size_t col = 0; col < 12; col++) {
        RgHeatLevel l = rg_heatmap_level(&h, rg_heatmap_cell(&h, 1, col), RgHeatMetricEvents);
        CHECK(l == ((col % 2) ? RgHeatNotObserved : RgHeatQuiet));
    }
    // 315 MHz: always listened, never active.
    for(size_t col = 0; col < 12; col++)
        CHECK(
            rg_heatmap_level(&h, rg_heatmap_cell(&h, 0, col), RgHeatMetricEvents) == RgHeatQuiet);
    // 433.92 (row 2) is active in intervals 0, 3, 6, 9.
    for(size_t col = 0; col < 12; col++) {
        RgHeatLevel l = rg_heatmap_level(&h, rg_heatmap_cell(&h, 2, col), RgHeatMetricEvents);
        CHECK((col % 3 == 0) ? l >= RgHeatLow : l == RgHeatQuiet);
    }
    // Strength: the peak rose 46 dB above the floor.
    CHECK(rg_heatmap_level(&h, rg_heatmap_cell(&h, 2, 0), RgHeatMetricStrength) == RgHeatHigh);
    char text[64];
    rg_heatmap_cell_text(&h, 1, 1, text, sizeof(text));
    CHECK(strstr(text, "390.00") && strstr(text, "not observed"));
    rg_heatmap_cell_text(&h, 2, 3, text, sizeof(text));
    CHECK(strstr(text, "433.92") && strstr(text, "burst"));
    rg_heatmap_col_time(&h, 3, text, sizeof(text));
    CHECK(strcmp(text, "0:30-0:40") == 0);
    rg_heatmap_cell_text(&h, 0, 0, text, sizeof(text));
    CHECK(strstr(text, "quiet, cov"));
    printf("  e.g. \"%s\"\n", text);
    // Beyond the log: no data, not quiet.
    CHECK(
        rg_heatmap_level(&h, rg_heatmap_cell(&h, 0, 40), RgHeatMetricEvents) == RgHeatNotObserved);
    rg_heatmap_cell_text(&h, 0, 40, text, sizeof(text));
    CHECK(strstr(text, "no data"));
    // Coverage: 13 of 50 visits a 10 s interval, 20 ms dwell... measured, not assumed.
    uint8_t cov = rg_heatmap_coverage(&h, rg_heatmap_cell(&h, 0, 0));
    CHECK(cov > 0 && cov <= 100);
}

static void test_merging(void) {
    printf("test_merging\n");
    build_log(600); // 6,000 s: 600 intervals into 64 columns
    static RgHeatmap h;
    map_log(&h, g_log, g_len, 1000);
    CHECK(h.cols_used <= RG_HEATMAP_COLS && h.span_s >= 6000u / RG_HEATMAP_COLS);
    CHECK(h.span_s % 10 == 0 && h.rows == 2400);
    // Bursts are kept through every merge.
    for(size_t row = 0; row < 4; row++) {
        uint32_t f = rg_heatmap_row_freq(&h, row), sum = 0, want = 0;
        for(size_t c = 0; c < RG_HEATMAP_COLS; c++)
            sum += rg_heatmap_cell(&h, row, c)->events;
        for(int i = 0; i < 4; i++)
            if(FREQS[i] == f) want = g_events[i];
        CHECK(sum == want);
    }
    // A merged 390 MHz column holds observed and unobserved intervals:
    // observed (some visits), with the coverage that says how little.
    const RgHeatCell* c = rg_heatmap_cell(&h, 1, 3);
    CHECK(rg_heatmap_level(&h, c, RgHeatMetricEvents) == RgHeatQuiet);
    printf(
        "  600 intervals -> %u columns of %lu s\n",
        (unsigned)h.cols_used,
        (unsigned long)h.span_s);
}

static void test_parts_and_junk(void) {
    printf("test_parts_and_junk\n");
    build_log(100);
    static RgHeatmap whole, parts;
    map_log(&whole, g_log, g_len, 64);
    // The two parts fed one after the other.
    rg_heatmap_probe_begin(&parts);
    rg_heatmap_feed(&parts, g_log, g_part2);
    rg_heatmap_end(&parts);
    RgHeatCell* cells = malloc(rg_heatmap_cells_bytes(&parts));
    rg_heatmap_begin(&parts, cells);
    rg_heatmap_feed(&parts, g_log, g_part2);
    rg_heatmap_end(&parts);
    rg_heatmap_feed(&parts, g_log + g_part2, g_len - g_part2);
    rg_heatmap_end(&parts);
    CHECK(parts.channels == whole.channels && parts.span_s == whole.span_s);
    CHECK(memcmp(cells, g_cells, rg_heatmap_cells_bytes(&whole)) == 0);
    free(cells);
    // Junk lines are skipped, the rest still read.
    const char* junk = "t_s,freq_hz,visits,observed_ms,active_visits,events,peak_dbm,floor_dbm\n"
                       "10,433920000,5,100,0,0,-90,-95\n"
                       "10,315000000,5,100,0,0,-91,-95\n"
                       "20,433920000,5,100,1,1,-40,-95\n"
                       "20,433920000,x,1,1,1,1,1\n"
                       "20,433920000,5,100,1,1,-40\n"
                       "20,433920000,5,100,1,1,-40,-95,7\n"
                       "20,999000000,5,100,1,1,-40,-95\n"
                       "garbage line\n"
                       "30,315000000,0,0,,,,\n"
                       "30,433920000,99999999999,100,1,1,-40,-95";
    static RgHeatmap j;
    map_log(&j, junk, strlen(junk), 5);
    CHECK(j.channels == 2 && j.rows == 4 && j.skipped == 5);
    CHECK(!j.ended);
    CHECK(
        rg_heatmap_level(&j, rg_heatmap_cell(&j, 0, 2), RgHeatMetricEvents) == RgHeatNotObserved);
    // An empty or header-only file has no frequencies.
    static RgHeatmap e;
    rg_heatmap_probe_begin(&e);
    rg_heatmap_feed(&e, "# just a comment\n", 17);
    rg_heatmap_end(&e);
    CHECK(e.channels == 0 && rg_heatmap_cells_bytes(&e) == 0);
}

int main(void) {
    test_states();
    test_merging();
    test_parts_and_junk();
    free(g_cells);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
