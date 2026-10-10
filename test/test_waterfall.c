/**
 * Host-side unit tests for the RSSI sweep history (helpers/rg_waterfall.c),
 * with the range planner (helpers/rg_range.c) for real point counts.
 * Run: make -C test
 */
#include "../helpers/rg_waterfall.h"
#include "../helpers/rg_range.h"
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

static uint8_t g_buf[16384];

/* One sweep with every point reading @p dbm, committed at @p t. */
static void sweep(RgWaterfall* wf, float dbm, uint32_t t) {
    for(uint32_t i = 0; i < wf->points; i++)
        rg_waterfall_add(wf, i, dbm);
    rg_waterfall_commit(wf, t);
}

static void test_layout(void) {
    printf("test_layout\n");
    RgWaterfall wf;
    CHECK(!rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 0), "no points refused");
    CHECK(!rg_waterfall_init(&wf, NULL, sizeof(g_buf), 10), "no buffer refused");
    CHECK(!rg_waterfall_init(&wf, g_buf, 10, 128), "buffer under one row refused");
    CHECK(rg_waterfall_rows_for(128, rg_waterfall_bytes(128, 50)) == 50, "bytes/rows round trip");
    CHECK(rg_waterfall_rows_for(10, rg_waterfall_bytes(10, 7)) == 7, "narrow round trip");
    CHECK(rg_waterfall_rows_for(1, 1u << 20) == RG_WF_MAX_ROWS, "rows capped");
    // Any alignment of the caller's buffer works.
    for(size_t off = 0; off < 4; off++) {
        size_t bytes = rg_waterfall_bytes(256, 40);
        CHECK(rg_waterfall_init(&wf, g_buf + off, bytes, 256), "init at an odd offset");
        CHECK(wf.rows == 40 && wf.columns == 128, "rows and columns");
        CHECK((uintptr_t)wf.stamp % 4 == 0, "stamps aligned");
        CHECK(
            wf.cells + (size_t)wf.rows * wf.columns <= g_buf + off + bytes,
            "cells inside the buffer");
        CHECK((uint8_t*)wf.stamp >= g_buf + off, "stamps inside the buffer");
    }
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 37), "37 points");
    CHECK(wf.columns == 37, "fewer points than columns: one column each");
}

static void test_wraparound(void) {
    printf("test_wraparound\n");
    RgWaterfall wf;
    CHECK(rg_waterfall_init(&wf, g_buf, rg_waterfall_bytes(16, 5), 16), "init");
    CHECK(wf.rows == 5, "5 rows");
    CHECK(rg_waterfall_cell(&wf, 0, 0) == RG_WF_NO_DATA, "empty history");
    for(int s = 0; s < 12; s++)
        sweep(&wf, -100.0f + (float)s, 1000u + (uint32_t)s);
    CHECK(wf.filled == 5 && wf.sweeps == 12, "keeps the last 5 of 12");
    for(uint16_t age = 0; age < 5; age++) {
        CHECK(rg_waterfall_cell(&wf, age, 3) == -100 + 11 - age, "newest first after wrap");
        CHECK(rg_waterfall_stamp(&wf, age) == 1011u - age, "stamps follow their rows");
    }
    CHECK(rg_waterfall_cell(&wf, 5, 3) == RG_WF_NO_DATA, "older than the buffer");
    CHECK(rg_waterfall_stamp(&wf, 5) == 0, "no stamp beyond");
    CHECK(rg_waterfall_max_scroll(&wf, 3) == 2, "scroll range");
    CHECK(rg_waterfall_max_scroll(&wf, 40) == 0, "fits: no scroll");
    rg_waterfall_clear(&wf);
    CHECK(
        wf.filled == 0 && wf.sweeps == 0 && rg_waterfall_cell(&wf, 0, 0) == RG_WF_NO_DATA,
        "clear");
}

static void test_columns(void) {
    printf("test_columns\n");
    RgWaterfall wf;
    // Variable widths: every point maps to a column, columns cover the points
    // without gaps or overlaps, in order.
    static const uint32_t widths[] = {1, 2, 3, 64, 100, 127, 128, 129, 200, 255, 256};
    for(size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
        uint32_t p = widths[w];
        CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), p), "init");
        uint32_t next = 0;
        bool ok = true;
        for(uint16_t c = 0; c < wf.columns; c++) {
            uint32_t first, last;
            rg_waterfall_points_of(&wf, c, &first, &last);
            if(first != next || last < first) ok = false;
            for(uint32_t i = first; i <= last; i++)
                if(rg_waterfall_column_of(&wf, i) != c) ok = false;
            next = last + 1;
        }
        CHECK(ok && next == p, "columns partition the points");
        // Pixel spans partition a 128 and a 100 pixel picture too.
        static const uint16_t pics[] = {128, 100};
        for(size_t k = 0; k < 2; k++) {
            uint16_t px = 0;
            bool pok = true;
            for(uint16_t c = 0; c < wf.columns; c++) {
                uint16_t x, n;
                rg_waterfall_column_span(&wf, c, pics[k], &x, &n);
                if(x != px) pok = false;
                for(uint16_t j = x; j < x + n; j++)
                    if(rg_waterfall_column_at(&wf, j, pics[k]) != c) pok = false;
                px = (uint16_t)(x + n);
            }
            CHECK(pok && px == pics[k], "pixel spans partition the picture");
        }
    }
    // More points than columns: a column keeps its strongest point.
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 256), "256 points");
    for(uint32_t i = 0; i < 256; i++)
        rg_waterfall_add(&wf, i, i == 101 ? -40.0f : -95.0f);
    rg_waterfall_commit(&wf, 1);
    CHECK(rg_waterfall_column_of(&wf, 101) == 50, "point 101 in column 50");
    CHECK(rg_waterfall_cell(&wf, 0, 50) == -40, "strongest point of the column");
    CHECK(rg_waterfall_cell(&wf, 0, 51) == -95, "neighbour column");
    // Out-of-range point indices are ignored, not clamped onto the edge.
    rg_waterfall_add(&wf, 256, -10.0f);
    rg_waterfall_add(&wf, 1000000, -10.0f);
    CHECK(!wf.cur_any, "out-of-range points ignored");
}

static void test_missing(void) {
    printf("test_missing\n");
    RgWaterfall wf;
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 8), "init");
    // A sweep that measured only some points: the rest are missing, not zero.
    rg_waterfall_add(&wf, 0, -80.0f);
    rg_waterfall_add(&wf, 5, -70.0f);
    CHECK(rg_waterfall_commit(&wf, 10), "partial sweep stored");
    CHECK(rg_waterfall_cell(&wf, 0, 0) == -80 && rg_waterfall_cell(&wf, 0, 5) == -70, "measured");
    CHECK(rg_waterfall_cell(&wf, 0, 1) == RG_WF_NO_DATA, "unmeasured cell is missing");
    CHECK(wf.missing == 6, "missing cells counted");
    // A sweep with no reading at all is not stored.
    CHECK(!rg_waterfall_commit(&wf, 20), "empty sweep dropped");
    CHECK(wf.filled == 1, "still one row");
    // Discarded readings (recalibration) never reach the history.
    rg_waterfall_add(&wf, 2, -30.0f);
    rg_waterfall_discard(&wf);
    sweep(&wf, -90.0f, 30);
    CHECK(rg_waterfall_cell(&wf, 0, 2) == -90, "discarded reading gone");
    uint16_t col = 99, age = 99;
    int16_t dbm = 0;
    CHECK(rg_waterfall_peak(&wf, &col, &age, &dbm), "peak found");
    CHECK(col == 5 && age == 1 && dbm == -70, "peak is the strongest stored reading");
    // A missing cell renders as the sparse dotted pattern, never as a level.
    RgWaterfallStyle st = {
        .span_db = 20,
        .strong_db = 30,
        .noise_comp = false,
        .floors = NULL,
        .floor_all = RG_WF_NO_FLOOR};
    uint8_t xbm[16 * 2];
    CHECK(rg_waterfall_render(&wf, &st, 1, xbm, 128, 2) == 1, "one row from scroll 1");
    // Column 1 is pixels 16..31 (8 columns over 128 px); dots at x%8==4 on even ages.
    bool dots = false, other = false;
    for(int x = 16; x < 32; x++) {
        bool on = xbm[x / 8] & (1u << (x % 8));
        if(on && x % 8 == 4) dots = true;
        if(on && x % 8 != 4) other = true;
    }
    // Age 1 is odd: the pattern sits on even ages only, so this row is blank.
    CHECK(!dots && !other, "missing cell on an odd age: blank");
    CHECK(rg_waterfall_missing_dot(4, 0) && !rg_waterfall_missing_dot(3, 0), "dot pattern");
    rg_waterfall_add(&wf, 0, -80.0f);
    rg_waterfall_commit(&wf, 40);
    rg_waterfall_render(&wf, &st, 0, xbm, 128, 1);
    for(int x = 16; x < 32; x++) {
        bool on = xbm[x / 8] & (1u << (x % 8));
        if(on && x % 8 == 4) dots = true;
        if(on && x % 8 != 4) other = true;
    }
    CHECK(dots && !other, "missing cell on an even age: dotted only");
}

static void test_levels(void) {
    printf("test_levels\n");
    RgWaterfallStyle st = {
        .span_db = 24, .strong_db = 30, .noise_comp = true, .floors = NULL, .floor_all = -100};
    CHECK(rg_waterfall_level(RG_WF_NO_DATA, -100, &st) == 0, "no data: 0");
    CHECK(rg_waterfall_level(-100, -100, &st) == 0, "at the floor: 0");
    CHECK(rg_waterfall_level(-120, -100, &st) == 0, "below the floor: 0");
    CHECK(rg_waterfall_level(-99, -100, &st) == 1, "just above: 1");
    CHECK(rg_waterfall_level(-88, -100, &st) == 6, "half the span: half");
    CHECK(rg_waterfall_level(-76, -100, &st) == RG_WF_LEVEL_MAX, "full span: max");
    CHECK(rg_waterfall_level(-71, -100, &st) == RG_WF_LEVEL_MAX, "past the span, below strong");
    CHECK(rg_waterfall_level(-70, -100, &st) == RG_WF_LEVEL_STRONG, "strong");
    // Monotonic in dBm.
    uint8_t prev = 0;
    bool mono = true;
    for(int d = -140; d <= 0; d++) {
        uint8_t l = rg_waterfall_level((int16_t)d, -100, &st);
        if(l < prev) mono = false;
        prev = l;
    }
    CHECK(mono, "levels rise with the reading");
    // Noise compensation: the same reading over a higher floor is weaker.
    CHECK(
        rg_waterfall_level(-80, -90, &st) < rg_waterfall_level(-80, -100, &st),
        "compensated against the column floor");
    // Unknown column floor: the overall floor, else the absolute reference.
    CHECK(rg_waterfall_level(-88, RG_WF_NO_FLOOR, &st) == 6, "falls back to floor_all");
    st.floor_all = RG_WF_NO_FLOOR;
    CHECK(
        rg_waterfall_level(RG_WF_ABS_REF_DBM + 12, RG_WF_NO_FLOOR, &st) == 6,
        "falls back to the absolute reference");
    st.noise_comp = false;
    st.floor_all = -60;
    CHECK(
        rg_waterfall_level(RG_WF_ABS_REF_DBM + 12, -60, &st) == 6,
        "compensation off ignores floors");
    // Sensitivity: a smaller span shows the same reading denser.
    st.span_db = 10;
    CHECK(rg_waterfall_level(RG_WF_ABS_REF_DBM + 5, 0, &st) == 6, "span 10");
    st.span_db = 0; // invalid span must not divide by zero
    CHECK(rg_waterfall_level(RG_WF_ABS_REF_DBM + 5, 0, &st) == RG_WF_LEVEL_MAX, "span 0 safe");
    st.strong_db = 0; // no strong highlight
    st.span_db = 20;
    CHECK(rg_waterfall_level(0, 0, &st) == RG_WF_LEVEL_MAX, "strong highlight off");

    // Dither densities: level L sets L of 16 pixels in every 4x4 block.
    for(uint8_t l = 0; l <= RG_WF_LEVEL_MAX; l++) {
        int on = 0;
        for(uint16_t y = 0; y < 4; y++)
            for(uint16_t x = 0; x < 4; x++)
                on += rg_waterfall_dither(l, (uint16_t)(x + 8), (uint16_t)(y + 4));
        CHECK(on == l, "dither density");
    }
    int on = 0;
    for(uint16_t y = 0; y < 4; y++)
        for(uint16_t x = 0; x < 4; x++)
            on += rg_waterfall_dither(RG_WF_LEVEL_STRONG, x, y);
    CHECK(on == 16, "strong is solid");
}

static void test_extremes(void) {
    printf("test_extremes\n");
    RgWaterfall wf;
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 4), "init");
    rg_waterfall_add(&wf, 0, -1000.0f);
    rg_waterfall_add(&wf, 1, 1000.0f);
    rg_waterfall_add(&wf, 2, -159.4f);
    rg_waterfall_add(&wf, 3, -127.0f);
    rg_waterfall_commit(&wf, 1);
    CHECK(rg_waterfall_cell(&wf, 0, 0) == 1 - RG_WF_CELL_BIAS, "very low clamps, stays measured");
    CHECK(rg_waterfall_cell(&wf, 0, 1) == 255 - RG_WF_CELL_BIAS, "very high clamps");
    CHECK(rg_waterfall_cell(&wf, 0, 2) == -159, "lowest exact value");
    CHECK(rg_waterfall_cell(&wf, 0, 3) == -127, "the scanner's no-reading value is a reading");
    RgWaterfallStyle st = {
        .span_db = 20, .strong_db = 25, .noise_comp = true, .floors = NULL, .floor_all = -128};
    CHECK(rg_waterfall_level(95, -128, &st) == RG_WF_LEVEL_STRONG, "extreme strong");
    CHECK(rg_waterfall_level(-159, 127, &st) == 0, "extreme weak over a high floor");
}

static void test_pause_resume(void) {
    printf("test_pause_resume\n");
    // A paused engine feeds nothing; the sweep in progress resumes where it
    // stopped and is stored once complete, with the stamp of its end.
    RgWaterfall wf;
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 6), "init");
    sweep(&wf, -90.0f, 100);
    rg_waterfall_add(&wf, 0, -60.0f);
    rg_waterfall_add(&wf, 1, -61.0f);
    // ... paused for a while: nothing is committed.
    CHECK(wf.filled == 1, "pause stores nothing");
    for(uint32_t i = 2; i < 6; i++)
        rg_waterfall_add(&wf, i, -62.0f);
    rg_waterfall_commit(&wf, 5000);
    CHECK(wf.filled == 2 && rg_waterfall_stamp(&wf, 0) == 5000, "resumed sweep stored");
    CHECK(
        rg_waterfall_cell(&wf, 0, 0) == -60 && rg_waterfall_cell(&wf, 0, 5) == -62,
        "readings before and after the pause");
    CHECK(wf.missing == 0, "nothing missing");
}

static void test_render(void) {
    printf("test_render\n");
    RgWaterfall wf;
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), 128), "init");
    int8_t floors[RG_WF_MAX_COLUMNS];
    for(int c = 0; c < 128; c++)
        floors[c] = -100;
    // Older sweep quiet, newest with a strong signal on column 64.
    sweep(&wf, -100.0f, 1);
    for(uint32_t i = 0; i < 128; i++)
        rg_waterfall_add(&wf, i, i == 64 ? -50.0f : -100.0f);
    rg_waterfall_commit(&wf, 2);
    RgWaterfallStyle st = {
        .span_db = 20, .strong_db = 30, .noise_comp = true, .floors = floors, .floor_all = -100};
    uint8_t xbm[16 * 42];
    memset(xbm, 0xAA, sizeof(xbm));
    CHECK(rg_waterfall_render(&wf, &st, 0, xbm, 128, 42) == 2, "two rows drawn");
    CHECK(xbm[8] & 1u, "strong cell set on the newest row (x=64)");
    CHECK(!(xbm[7] & 0x80u) && !(xbm[8] & 2u), "neighbours blank");
    bool blank = true;
    for(size_t i = 16; i < sizeof(xbm); i++)
        if(xbm[i]) blank = false;
    CHECK(blank, "quiet and unfilled rows blank (stale bytes cleared)");
    // Scrolling one row back shows the quiet sweep on top.
    rg_waterfall_render(&wf, &st, 1, xbm, 128, 42);
    CHECK(xbm[8] == 0, "scrolled");
    // A narrow picture and an odd width stay inside the bitmap.
    uint8_t small[2 * 3];
    CHECK(rg_waterfall_render(&wf, &st, 0, small, 13, 3) == 2, "13 x 3 picture");
    CHECK((small[0] | small[1] | small[2] | small[3] | small[4] | small[5]) == 0, "quiet: blank");
    CHECK(rg_waterfall_render(&wf, &st, 50, xbm, 128, 42) == 0, "scroll past the end: blank");
}

static void test_ranges(void) {
    printf("test_ranges\n");
    // Empty and invalid frequency ranges never reach the waterfall: the plan
    // refuses them and a zero-point history refuses to initialise.
    RgBandSet bands = {.band = {{300000000, 348000000}, {387000000, 464000000}}, .count = 2};
    RgRange r;
    CHECK(
        rg_range_plan(&r, 433000000, 432000000, 10000, &bands, 256) == RgRangeErrorOrder, "order");
    CHECK(
        rg_range_plan(&r, 350000000, 380000000, 100000, &bands, 256) == RgRangeErrorNoPoints,
        "gap");
    CHECK(
        rg_range_plan(&r, 300000000, 464000000, 1000, &bands, 256) == RgRangeErrorTooMany, "many");
    RgWaterfall wf;
    CHECK(!rg_waterfall_init(&wf, g_buf, sizeof(g_buf), r.points * 0), "zero points refused");
    // A plan across a gap: the waterfall's columns follow the tunable points.
    CHECK(
        rg_range_plan(&r, 340000000, 400000000, 1000000, &bands, 256) == RgRangeOk, "across gap");
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), r.points), "init over the plan");
    CHECK(wf.columns == r.points, "one column per tunable point");
    CHECK(r.points == 9 + 14, "348-387 skipped");
    // A single point: one column, the whole picture wide.
    CHECK(rg_range_plan(&r, 433920000, 433920000, 1000, &bands, 256) == RgRangeOk, "one point");
    CHECK(rg_waterfall_init(&wf, g_buf, sizeof(g_buf), r.points) && wf.columns == 1, "1 column");
    uint16_t x, w;
    rg_waterfall_column_span(&wf, 0, 128, &x, &w);
    CHECK(x == 0 && w == 128, "spans the picture");
}

int main(void) {
    test_layout();
    test_wraparound();
    test_columns();
    test_missing();
    test_levels();
    test_extremes();
    test_pause_resume();
    test_render();
    test_ranges();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
