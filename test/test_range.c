/**
 * Host-side unit tests for receive bands and range-scan planning
 * (helpers/rg_range.c). The two band tables are the firmwares' own
 * furi_hal_subghz_is_frequency_valid() ranges, copied from their sources:
 * Official 1.4.3 and RogueMaster / Momentum / Unleashed.
 * Run: make -C test
 */
#include "../helpers/rg_range.h"
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

/* Official firmware 1.4.3, targets/f7/furi_hal/furi_hal_subghz.c. */
static bool official_valid(uint32_t v, void* ctx) {
    unsigned* calls = ctx;
    if(calls) (*calls)++;
    return (v >= 299999755 && v <= 348000335) || (v >= 386999938 && v <= 464000000) ||
           (v >= 778999847 && v <= 928000000);
}

/* RogueMaster, Momentum mntm-012 and Unleashed unlshd-093. */
static bool cfw_valid(uint32_t v, void* ctx) {
    (void)ctx;
    return (v >= 281000000 && v <= 361000000) || (v >= 378000000 && v <= 481000000) ||
           (v >= 749000000 && v <= 962000000);
}

static bool none_valid(uint32_t v, void* ctx) {
    (void)v;
    (void)ctx;
    return false;
}

/* One continuous range across the first two candidates. */
static bool wide_valid(uint32_t v, void* ctx) {
    (void)ctx;
    return (v >= 281000000 && v <= 481000000) || (v >= 749000000 && v <= 962000000);
}

/* Only the middle band, and only its upper half. */
static bool odd_valid(uint32_t v, void* ctx) {
    (void)ctx;
    return v >= 440000000 && v <= 470000000;
}

static void test_probe(void) {
    printf("test_probe\n");
    RgBandSet set;
    unsigned calls = 0;
    rg_range_probe_bands(rg_range_cc1101_bands, 3, official_valid, &calls, &set);
    CHECK(set.count == 3, "official: three bands");
    CHECK(set.band[0].lo_hz == 299999755 && set.band[0].hi_hz == 348000335, "official 315 band");
    CHECK(set.band[1].lo_hz == 386999938 && set.band[1].hi_hz == 464000000, "official 433 band");
    CHECK(set.band[2].lo_hz == 778999847 && set.band[2].hi_hz == 928000000, "official 868 band");
    // Binary search: about 2 x 27 checks per band plus anchors.
    CHECK(calls < 3 * 70, "probe needs few driver calls");

    rg_range_probe_bands(rg_range_cc1101_bands, 3, cfw_valid, NULL, &set);
    CHECK(set.count == 3, "cfw: three bands");
    CHECK(set.band[0].lo_hz == 281000000 && set.band[0].hi_hz == 361000000, "cfw 315 band");
    CHECK(set.band[1].lo_hz == 378000000 && set.band[1].hi_hz == 481000000, "cfw 433 band");
    CHECK(set.band[2].lo_hz == 749000000 && set.band[2].hi_hz == 962000000, "cfw 868 band");

    rg_range_probe_bands(rg_range_cc1101_bands, 3, none_valid, NULL, &set);
    CHECK(set.count == 0, "no radio: no bands");

    // The middle of the candidate is rejected: found through the other anchors.
    rg_range_probe_bands(rg_range_cc1101_bands, 3, odd_valid, NULL, &set);
    CHECK(set.count == 1, "odd driver: one band");
    CHECK(set.band[0].lo_hz == 440000000 && set.band[0].hi_hz == 470000000, "odd band edges");

    // Unsorted, overlapping candidates are sorted and merged.
    RgBand cand[3] = {{749000000, 962000000}, {281000000, 400000000}, {378000000, 481000000}};
    rg_range_probe_bands(cand, 3, wide_valid, NULL, &set);
    CHECK(set.count == 2, "overlaps merged");
    CHECK(set.band[0].lo_hz == 281000000 && set.band[0].hi_hz == 481000000, "merged lower part");
    CHECK(set.band[1].lo_hz == 749000000 && set.band[1].hi_hz == 962000000, "sorted upper part");

    CHECK(rg_range_in_bands(&set, 433920000), "433.92 in band");
    CHECK(!rg_range_in_bands(&set, 600000000), "600 MHz not in band");
}

static void test_plan(void) {
    printf("test_plan\n");
    RgBandSet off;
    rg_range_probe_bands(rg_range_cc1101_bands, 3, official_valid, NULL, &off);
    RgRange r;

    // 433.00-435.00 MHz at 25 kHz: 81 points, all tunable.
    CHECK(rg_range_plan(&r, 433000000, 435000000, 25000, &off, 256) == RgRangeOk, "433 plan ok");
    CHECK(r.points == 81 && r.grid_points == 81 && r.segments == 1, "433: 81 points");
    CHECK(rg_range_frequency(&r, 0) == 433000000, "first point");
    CHECK(rg_range_frequency(&r, 80) == 435000000, "last point");
    CHECK(rg_range_frequency(&r, 81) == 0, "past the end");
    CHECK(rg_range_frequency(&r, 37) == 433925000, "point 37");
    CHECK(rg_range_nearest_index(&r, 433920000) == 37, "nearest to 433.92");
    CHECK(rg_range_nearest_index(&r, 100000000) == 0, "nearest below");
    CHECK(rg_range_nearest_index(&r, 999000000) == 80, "nearest above");

    // Across all three Official bands, 1 MHz steps from 300 to 928 MHz: the
    // 349-386 and 465-778 MHz gaps are skipped.
    CHECK(rg_range_plan(&r, 300000000, 928000000, 1000000, &off, 1000) == RgRangeOk, "wide ok");
    CHECK(r.grid_points == 629, "wide: grid points");
    CHECK(r.segments == 3, "wide: three segments");
    // 300..348 (49), 387..464 (78), 779..928 (150).
    CHECK(r.points == 49 + 78 + 150, "wide: tunable points");
    for(uint32_t i = 0; i < r.points; i++) {
        uint32_t f = rg_range_frequency(&r, i);
        if(!official_valid(f, NULL)) {
            printf("  point %u = %u Hz is in a gap\n", (unsigned)i, (unsigned)f);
            CHECK(false, "every point tunable");
            break;
        }
        if(i > 0 && f <= rg_range_frequency(&r, i - 1)) {
            CHECK(false, "points ascending");
            break;
        }
    }
    CHECK(rg_range_frequency(&r, 48) == 348000000, "last of the 315 band");
    CHECK(rg_range_frequency(&r, 49) == 387000000, "first of the 433 band");
    CHECK(rg_range_frequency(&r, 49 + 78) == 779000000, "first of the 868 band");
    CHECK(rg_range_nearest_index(&r, 360000000) == 48, "gap: nearest is the band edge below");
    CHECK(rg_range_nearest_index(&r, 380000000) == 49, "gap: nearest is the band edge above");

    // The same request is refused with a limit, but the counts are kept.
    CHECK(
        rg_range_plan(&r, 300000000, 928000000, 1000000, &off, 256) == RgRangeErrorTooMany,
        "too many points");
    CHECK(r.points == 277, "counts kept when refused");

    CHECK(rg_range_plan(&r, 435000000, 433000000, 25000, &off, 256) == RgRangeErrorOrder, "order");
    CHECK(rg_range_plan(&r, 433000000, 435000000, 0, &off, 256) == RgRangeErrorStep, "zero step");
    CHECK(
        rg_range_plan(&r, 433000000, 435000000, 999, &off, 256) == RgRangeErrorStep, "1 kHz min");
    CHECK(
        rg_range_plan(&r, 433000000, 435000000, 10000001, &off, 256) == RgRangeErrorStep,
        "10 MHz max");
    // Entirely inside a gap.
    CHECK(
        rg_range_plan(&r, 500000000, 700000000, 100000, &off, 256) == RgRangeErrorNoPoints,
        "gap only");
    // A gap narrower than the step lands no point in the band piece.
    RgBandSet thin = {.band = {{433000500, 433000900}}, .count = 1};
    CHECK(
        rg_range_plan(&r, 433000000, 433002000, 1000, &thin, 256) == RgRangeErrorNoPoints,
        "band narrower than a step");
    // Single point.
    CHECK(rg_range_plan(&r, 433920000, 433920000, 25000, &off, 256) == RgRangeOk, "one point");
    CHECK(r.points == 1 && rg_range_frequency(&r, 0) == 433920000, "single point frequency");
    // End not on the grid: the last point is below it.
    CHECK(rg_range_plan(&r, 433000000, 433010000, 3000, &off, 256) == RgRangeOk, "off-grid end");
    CHECK(r.points == 4 && rg_range_frequency(&r, 3) == 433009000, "off-grid end last point");
    // Whole 32-bit span at the largest step does not overflow.
    CHECK(
        rg_range_plan(&r, 0, UINT32_MAX, RG_RANGE_MAX_STEP_HZ, &off, 1000) == RgRangeOk,
        "full span");
    CHECK(r.grid_points == UINT32_MAX / RG_RANGE_MAX_STEP_HZ + 1, "full span grid");
    for(uint32_t i = 0; i < r.points; i++) {
        CHECK(official_valid(rg_range_frequency(&r, i), NULL), "full span points tunable");
    }

    // The CFW bands give more points over the same request.
    RgBandSet cfw;
    rg_range_probe_bands(rg_range_cc1101_bands, 3, cfw_valid, NULL, &cfw);
    CHECK(rg_range_plan(&r, 281000000, 962000000, 5000000, &cfw, 256) == RgRangeOk, "cfw wide");
    // 281..361 (17), 381..481 (21), 751..961 (43): the grid starts at 281.
    CHECK(r.points == 17 + 21 + 43, "cfw wide points");
}

static void test_sweep(void) {
    printf("test_sweep\n");
    CHECK(rg_range_sweep_ms(81, 5) == 81 * (5 + RG_RANGE_PROBE_OVERHEAD_MS), "81 x 5 ms");
    CHECK(rg_range_sweep_ms(0, 50) == 0, "no points");
    CHECK(rg_range_sweep_ms(UINT32_MAX, UINT32_MAX) == UINT32_MAX, "saturates");
}

static void test_step(void) {
    printf("test_step\n");
    RgBandSet off;
    rg_range_probe_bands(rg_range_cc1101_bands, 3, official_valid, NULL, &off);
    CHECK(rg_range_step(&off, 433920000, 25000) == 433945000, "in band up");
    CHECK(rg_range_step(&off, 433920000, -1000) == 433919000, "in band down");
    // Off the top of the 315 band: on to the bottom of the 433 band (whole kHz).
    CHECK(rg_range_step(&off, 348000000, 1000000) == 387000000, "gap up");
    CHECK(rg_range_step(&off, 387000000, -100000) == 348000000, "gap down");
    CHECK(rg_range_step(&off, 464000000, 1000) == 779000000, "second gap up");
    // Past the last band: stays on its edge.
    CHECK(rg_range_step(&off, 928000000, 1000000) == 928000000, "top edge");
    CHECK(rg_range_step(&off, 300000000, -1000000) == 300000000, "bottom edge");
    // Outside every band: snap to the nearest edge first.
    CHECK(rg_range_step(&off, 360000000, 0) == 348000000, "snap down");
    CHECK(rg_range_step(&off, 380000000, 0) == 387000000, "snap up");
    CHECK(rg_range_step(&off, 380000000, 1000) == 387001000, "snap then step");
    RgBandSet empty = {.count = 0};
    CHECK(rg_range_step(&empty, 433920000, 1000) == 433920000, "no bands: unchanged");
}

static void test_text(void) {
    printf("test_text\n");
    CHECK(strcmp(rg_range_result_text(RgRangeOk), "OK") == 0, "ok text");
    CHECK(strcmp(rg_range_result_text(RgRangeErrorTooMany), "Too many points") == 0, "too many");
    CHECK(strcmp(rg_range_result_text((RgRangeResult)99), "Error") == 0, "unknown result");
}

int main(void) {
    test_probe();
    test_plan();
    test_sweep();
    test_step();
    test_text();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
