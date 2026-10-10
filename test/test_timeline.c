/**
 * Host-side tests for the pulse timeline maths (helpers/rg_timeline.c).
 * Build & run via `make -C test check`. Inputs are synthetic.
 */
#include "../helpers/rg_timeline.h"
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

static void test_raster(void) {
    printf("test_raster\n");
    uint8_t cols[16];
    /* 100 us high, 300 us low, 50 us high, at 50 us per column. */
    const int32_t s[] = {100, -300, 50};
    rg_timeline_raster(s, 3, 0, 0, 50, cols, 16);
    CHECK(cols[0] == RG_TL_HIGH && cols[1] == RG_TL_HIGH, "high pulse spans two columns");
    CHECK(cols[2] == RG_TL_LOW && cols[7] == RG_TL_LOW, "low spans columns 2-7");
    CHECK(cols[8] == RG_TL_HIGH, "final pulse");
    CHECK(cols[9] == 0 && cols[15] == 0, "no data after the samples");

    rg_timeline_raster(s, 3, 0, 0, 1000, cols, 4);
    CHECK(cols[0] == (RG_TL_HIGH | RG_TL_LOW), "both levels inside one wide column");
    CHECK(cols[1] == 0, "nothing beyond 450 us");

    /* Window starting later than the screen, and a screen starting mid-pulse. */
    rg_timeline_raster(s, 3, 1000, 900, 50, cols, 16);
    CHECK(cols[0] == 0 && cols[1] == 0, "columns before the window are empty");
    CHECK(cols[2] == RG_TL_HIGH, "window data placed at its time");
    rg_timeline_raster(s, 3, 0, 75, 50, cols, 4);
    CHECK(cols[0] == (RG_TL_HIGH | RG_TL_LOW), "screen edge inside a pulse");

    const int32_t big[] = {2000000000, -2000000000};
    rg_timeline_raster(big, 2, 0, 1999999990, 5, cols, 16);
    CHECK(cols[1] == RG_TL_HIGH && cols[2] == RG_TL_LOW, "large times do not overflow");
}

static void test_labels(void) {
    printf("test_labels\n");
    RgTlLabel l[8];
    const int32_t s[] = {350, -1050, 1050, -350, 350};
    size_t n = rg_timeline_labels(s, 5, 0, 0, 20, 128, 20, l, 8);
    CHECK(n == 2, "only pulses at least 20 columns wide get labels");
    CHECK(l[0].us == 1050 && !l[0].high && l[0].x == (350 + 525) / 20, "low label centred");
    CHECK(l[1].us == 1050 && l[1].high, "high label");
    n = rg_timeline_labels(s, 5, 0, 400, 20, 128, 20, l, 8);
    CHECK(n == 1 && l[0].high, "pulse cut by the left edge gets no label");
    CHECK(rg_timeline_labels(s, 5, 0, 0, 20, 128, 20, l, 1) == 1, "output bounded");
}

static void test_navigation(void) {
    printf("test_navigation\n");
    const uint64_t total = 1000000; /* 1 s */
    CHECK(rg_timeline_clamp(5000, 1000, total) == 5000, "inside stays");
    CHECK(rg_timeline_clamp(999500, 1000, total) == 999000, "clamped to the end");
    CHECK(rg_timeline_clamp(500, 2000000, total) == 0, "short recording pins to 0");

    uint64_t left = rg_timeline_pan(0, -1, 50, 128, total);
    CHECK(left == 0, "cannot pan before the start");
    left = rg_timeline_pan(0, +1, 50, 128, total);
    CHECK(left == 1600, "pan is a quarter screen (6400/4)");
    left = rg_timeline_pan(left, -1, 50, 128, total);
    CHECK(left == 0, "pan back");

    int zoom = 3; /* 50 us/px */
    left = 10000;
    uint64_t centre = left + 50 * 128 / 2;
    CHECK(rg_timeline_zoom(&zoom, -1, &left, 128, total) && zoom == 2, "zoom in one step");
    CHECK(left + 20 * 128 / 2 == centre, "zoom keeps the centre");
    zoom = 0;
    CHECK(
        !rg_timeline_zoom(&zoom, -1, &left, 128, total) && zoom == 0, "finest zoom is the limit");
    zoom = RG_TL_ZOOM_COUNT - 1;
    CHECK(!rg_timeline_zoom(&zoom, +1, &left, 128, total), "coarsest zoom is the limit");
    zoom = 8;
    left = 0;
    rg_timeline_zoom(&zoom, +1, &left, 128, total);
    CHECK(left == 0, "zooming out past the recording pins to 0");

    CHECK(rg_timeline_zoom_for(12000, 128) == 4, "100 us/px is the finest step showing 12 ms");
    CHECK(rg_timeline_zoom_for(14000, 128) == 5, "14 ms needs 200 us/px");
    CHECK(rg_timeline_zoom_for(100, 128) == 0, "tiny detail -> finest");
    CHECK(rg_timeline_zoom_for(100000000, 128) == RG_TL_ZOOM_COUNT - 1, "huge -> coarsest");
}

static void test_covered(void) {
    printf("test_covered\n");
    CHECK(rg_timeline_covered(0, 5000, false, 0, 4000), "inside the window");
    CHECK(!rg_timeline_covered(0, 5000, false, 2000, 4000), "runs past the window end");
    CHECK(rg_timeline_covered(0, 5000, true, 2000, 4000), "window reaches the recording end");
    CHECK(!rg_timeline_covered(3000, 9000, false, 2000, 1000), "starts before the window");
}

static void test_frames(void) {
    printf("test_frames\n");
    const uint64_t starts[] = {10000, 50000, 90000};
    const uint64_t span = 8000; /* marker 1000 us into the screen */
    uint64_t left = 0;
    CHECK(rg_timeline_frame_at(starts, 3, 0, span) == 3, "before the first frame");
    size_t f = rg_timeline_frame_step(starts, 3, 0, span, +1, &left);
    CHECK(f == 0 && left == 9000, "next puts frame 1 on the marker");
    CHECK(rg_timeline_frame_at(starts, 3, left, span) == 0, "frame 1 is current");
    f = rg_timeline_frame_step(starts, 3, left, span, +1, &left);
    CHECK(f == 1 && left == 49000, "next frame");
    f = rg_timeline_frame_step(starts, 3, left, span, -1, &left);
    CHECK(f == 0 && left == 9000, "previous frame");
    uint64_t keep = left;
    f = rg_timeline_frame_step(starts, 3, left, span, -1, &left);
    CHECK(f == 3 && left == keep, "no frame before the first; position kept");
    f = rg_timeline_frame_step(starts, 3, 89000, span, +1, &left);
    CHECK(f == 3, "no frame after the last");
    CHECK(rg_timeline_frame_step(starts, 0, 0, span, +1, &left) == 0, "empty list");
}

int main(void) {
    test_raster();
    test_labels();
    test_navigation();
    test_covered();
    test_frames();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("TIMELINE TESTS FAILED\n");
        return 1;
    }
    printf("ALL TIMELINE TESTS PASSED\n");
    return 0;
}
