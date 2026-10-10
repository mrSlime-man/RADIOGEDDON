/**
 * Host-side unit tests for range-scan display maths (helpers/rg_spectrum.c).
 * Run: make -C test
 */
#include "../helpers/rg_spectrum.h"
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

static RgSpectrumPoint point(int8_t last, int8_t peak, int8_t floor, uint8_t flags) {
    RgSpectrumPoint p = {last, peak, floor, 0, flags};
    return p;
}

static void test_from_channel(void) {
    printf("test_from_channel\n");
    RgScanParams params;
    rg_scan_params_default(&params);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    RgSpectrumPoint p;
    rg_spectrum_from_channel(&p, &ch);
    CHECK(p.flags == 0 && p.last == RG_SPECTRUM_NONE && p.peak == RG_SPECTRUM_NONE, "unmeasured");

    rg_scan_channel_update(&ch, -90.4f, &params, 0);
    rg_spectrum_from_channel(&p, &ch);
    CHECK(p.flags == RgSpectrumMeasured && p.last == -90, "measured, rounded");
    for(int i = 0; i < 5; i++)
        rg_scan_channel_update(&ch, -90.6f, &params, i);
    rg_spectrum_from_channel(&p, &ch);
    CHECK((p.flags & RgSpectrumWarm) && p.last == -91, "warm after warm-up, rounds away from 0");
    rg_scan_channel_update(&ch, -40.0f, &params, 10);
    rg_spectrum_from_channel(&p, &ch);
    CHECK((p.flags & RgSpectrumActive) && p.hits == 1 && p.peak == -40, "active with a hit");
    // Counters saturate in the compact form.
    ch.hits = 1000;
    rg_spectrum_from_channel(&p, &ch);
    CHECK(p.hits == 255, "hits saturate at 255");
    // Extremes stay clear of the "no reading" value.
    ch.last = -500.0f;
    ch.peak = 500.0f;
    rg_spectrum_from_channel(&p, &ch);
    CHECK(p.last == -127 && p.peak == 127, "clamped");
}

static void test_floor(void) {
    printf("test_floor\n");
    RgSpectrumPoint pts[300];
    CHECK(rg_spectrum_median_floor(pts, 0) == RG_SPECTRUM_NONE, "no points");
    for(int i = 0; i < 300; i++)
        pts[i] = point(-90, -90, -90, RgSpectrumMeasured);
    CHECK(rg_spectrum_median_floor(pts, 300) == RG_SPECTRUM_NONE, "none warm");
    // The median covers every point, not just the first 32: points 0..99 are
    // noisy (-60), 100..299 quiet (-100): the median is quiet.
    for(int i = 0; i < 300; i++) {
        pts[i] = point(-80, -80, i < 100 ? -60 : -100, RgSpectrumMeasured | RgSpectrumWarm);
    }
    CHECK(rg_spectrum_median_floor(pts, 300) == -100, "median over all points");
    pts[0] = point(0, 0, -50, RgSpectrumMeasured | RgSpectrumWarm);
    CHECK(rg_spectrum_median_floor(pts, 1) == -50, "single point");
    pts[1] = point(0, 0, -70, RgSpectrumMeasured | RgSpectrumWarm);
    CHECK(rg_spectrum_median_floor(pts, 2) == -70, "lower median of two");
}

static void test_strongest(void) {
    printf("test_strongest\n");
    RgSpectrumPoint pts[4] = {
        point(RG_SPECTRUM_NONE, RG_SPECTRUM_NONE, RG_SPECTRUM_NONE, 0),
        point(-80, -70, -90, RgSpectrumMeasured),
        point(-80, -50, -90, RgSpectrumMeasured),
        point(-80, -50, -90, RgSpectrumMeasured),
    };
    CHECK(rg_spectrum_strongest(pts, 4) == 2, "first of the strongest");
    CHECK(rg_spectrum_strongest(pts, 1) == 1, "none measured: count");
    CHECK(rg_spectrum_strongest(pts, 0) == 0, "empty");
}

static void test_columns(void) {
    printf("test_columns\n");
    enum {
        W = 128
    };
    int8_t last[W], peak[W];
    uint8_t flags[W];
    RgSpectrumPoint pts[256];
    for(int i = 0; i < 256; i++)
        pts[i] = point(-100, -95, -100, RgSpectrumMeasured);
    pts[37] = point(-40, -30, -100, RgSpectrumMeasured | RgSpectrumActive);

    // More points than columns: two points a column, the stronger shown.
    rg_spectrum_columns(pts, 256, W, last, peak, flags);
    CHECK(last[18] == -40 && peak[18] == -30 && (flags[18] & RgSpectrumActive), "strong point");
    CHECK(last[17] == -100 && !(flags[17] & RgSpectrumActive), "neighbour column");
    CHECK(rg_spectrum_column_of(37, 256, W) == 18, "column of point 37");
    CHECK(rg_spectrum_column_of(255, 256, W) == 127, "last point, last column");
    CHECK(rg_spectrum_column_of(999, 256, W) == 127, "out of range clamps");

    // Fewer points than columns: every column shows some point; each point's
    // own column (as column_of says) shows that point.
    rg_spectrum_columns(pts, 81, W, last, peak, flags);
    for(int c = 0; c < W; c++) {
        CHECK(last[c] != RG_SPECTRUM_NONE, "no empty column");
    }
    for(size_t i = 0; i < 81; i++) {
        size_t c = rg_spectrum_column_of(i, 81, W);
        CHECK(last[c] == pts[i].last, "point drawn in its column");
    }
    // Between W/2 and W points (the case a plain formula gets wrong).
    for(size_t n = 1; n <= 256; n++) {
        rg_spectrum_columns(pts, n, W, last, peak, flags);
        size_t prev = 0;
        for(size_t i = 0; i < n; i++) {
            size_t c = rg_spectrum_column_of(i, n, W);
            if(c < prev) {
                CHECK(false, "columns ascend with the point");
                break;
            }
            prev = c;
            if(i == 37 && n > 37) CHECK(last[c] == -40, "point 37 in its column for every count");
        }
    }
    // Unmeasured points: an empty column.
    pts[0] = point(RG_SPECTRUM_NONE, RG_SPECTRUM_NONE, RG_SPECTRUM_NONE, 0);
    rg_spectrum_columns(pts, 1, W, last, peak, flags);
    CHECK(last[0] == RG_SPECTRUM_NONE && flags[0] == 0, "unmeasured column");
    rg_spectrum_columns(pts, 0, W, last, peak, flags);
    CHECK(last[5] == RG_SPECTRUM_NONE, "no points");
    CHECK(rg_spectrum_column_of(0, 0, W) == 0, "column of nothing");
}

int main(void) {
    test_from_channel();
    test_floor();
    test_strongest();
    test_columns();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
