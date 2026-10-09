/**
 * Host-side unit tests for the narrowband RSSI scanner logic (helpers/rg_scan.c).
 * All inputs are synthetic RSSI sequences, not radio measurements.
 * Run: make -C test
 */
#include "../helpers/rg_scan.h"
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

static void feed(RgScanChannel* ch, const RgScanParams* p, float rssi, int n, int* starts) {
    for(int i = 0; i < n; i++) {
        if(rg_scan_channel_update(ch, rssi, p, (uint32_t)i) && starts) (*starts)++;
    }
}

static void test_quiet_channel_no_activity(void) {
    printf("test_quiet_channel_no_activity\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    int starts = 0;
    float noise[] = {-98, -97, -99, -98, -96, -98, -97, -99, -98, -97};
    for(int r = 0; r < 20; r++)
        for(size_t i = 0; i < sizeof(noise) / sizeof(noise[0]); i++)
            if(rg_scan_channel_update(&ch, noise[i], &p, 0)) starts++;
    CHECK(starts == 0, "noise within a few dB never triggers");
    CHECK(!ch.active, "channel not active");
    CHECK(ch.floor > -100.0f && ch.floor < -96.0f, "floor settles inside the noise band");
    CHECK(ch.peak == -96.0f, "peak hold keeps the highest reading");
}

static void test_burst_detected_once(void) {
    printf("test_burst_detected_once\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    int starts = 0;
    feed(&ch, &p, -98.0f, 20, &starts);
    feed(&ch, &p, -60.0f, 15, &starts); // a transmission lasting several readings
    CHECK(starts == 1, "one burst counts as one activity start");
    CHECK(ch.active, "active during the burst");
    CHECK(ch.floor < -95.0f, "floor frozen while active");
    feed(&ch, &p, -98.0f, 3, &starts);
    CHECK(!ch.active, "activity ends when the burst ends");
    feed(&ch, &p, -60.0f, 2, &starts);
    CHECK(starts == 2, "a second burst counts again");
    CHECK(ch.hits == 2, "hit counter matches");
    CHECK(ch.peak == -60.0f, "peak is the burst level");
}

static void test_warmup_suppresses_detection(void) {
    printf("test_warmup_suppresses_detection\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    int starts = 0;
    feed(&ch, &p, -50.0f, RG_SCAN_WARMUP_SAMPLES - 1, &starts);
    CHECK(starts == 0, "no detection before the floor is established");
    CHECK(ch.samples == RG_SCAN_WARMUP_SAMPLES - 1, "samples counted during warm-up");
}

static void test_hysteresis(void) {
    printf("test_hysteresis\n");
    RgScanParams p;
    rg_scan_params_default(&p); // +10 dB on, 3 dB hysteresis
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    int starts = 0;
    feed(&ch, &p, -100.0f, 10, &starts);
    // Hovering around the threshold must not chatter.
    float seq[] = {-89, -91, -89.5f, -91.5f, -89, -92};
    for(size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
        if(rg_scan_channel_update(&ch, seq[i], &p, 0)) starts++;
    CHECK(starts == 1, "hovering near the threshold is one activity period");
    CHECK(ch.active, "still active above the off level");
    rg_scan_channel_update(&ch, -94.0f, &p, 0);
    CHECK(!ch.active, "drops below the off level ends activity");
}

static void test_abs_min(void) {
    printf("test_abs_min\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    p.abs_min_dbm = -80.0f;
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    int starts = 0;
    feed(&ch, &p, -110.0f, 10, &starts);
    feed(&ch, &p, -85.0f, 1, &starts);
    CHECK(starts == 0, "a rise that stays below abs_min is ignored");
}

static void test_floor_follows_slow_rise(void) {
    printf("test_floor_follows_slow_rise\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    feed(&ch, &p, -100.0f, 10, NULL);
    // Noise rises 5 dB (e.g. nearby interference); floor should follow.
    feed(&ch, &p, -95.0f, 200, NULL);
    CHECK(ch.floor > -95.5f, "floor tracks a sustained rise below threshold");
    feed(&ch, &p, -105.0f, 10, NULL);
    CHECK(ch.floor < -104.0f, "floor falls quickly to quieter readings");
}

static void test_reset_peak(void) {
    printf("test_reset_peak\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    feed(&ch, &p, -98.0f, 10, NULL);
    feed(&ch, &p, -50.0f, 1, NULL);
    feed(&ch, &p, -98.0f, 3, NULL);
    float floor_before = ch.floor;
    rg_scan_channel_reset_peak(&ch);
    CHECK(ch.hits == 0, "hits cleared");
    CHECK(ch.peak == -98.0f, "peak restarts from the last reading");
    CHECK(ch.floor == floor_before, "floor kept");
}

static void test_global_floor(void) {
    printf("test_global_floor\n");
    RgScanParams p;
    rg_scan_params_default(&p);
    RgScanChannel chs[4];
    for(int i = 0; i < 4; i++)
        rg_scan_channel_reset(&chs[i]);
    CHECK(rg_scan_global_floor(chs, 4) == RG_SCAN_RSSI_NONE, "no floor before warm-up");
    feed(&chs[0], &p, -100.0f, 10, NULL);
    feed(&chs[1], &p, -90.0f, 10, NULL);
    feed(&chs[2], &p, -95.0f, 10, NULL);
    // chs[3] stays cold and must be ignored.
    float g = rg_scan_global_floor(chs, 4);
    CHECK(g > -95.5f && g < -94.5f, "median of three warmed floors");
}

static void test_band_mask(void) {
    printf("test_band_mask\n");
    const uint32_t f[] = {315000000, 433920000, 868350000, 433075000};
    CHECK(rg_scan_band_mask(f, 4, 387000000, 464000000) == 0xA, "433 band selects 1 and 3");
    CHECK(rg_scan_band_mask(f, 4, 779000000, 928000000) == 0x4, "868 band selects 2");
    CHECK(rg_scan_band_mask(f, 0, 0, 0xFFFFFFFF) == 0, "empty list -> empty mask");
}

static void test_format_row(void) {
    printf("test_format_row\n");
    RgScanChannel ch;
    rg_scan_channel_reset(&ch);
    ch.last = -97.25f;
    ch.peak = -61.0f;
    ch.floor = -98.04f;
    ch.hits = 3;
    char buf[64];
    size_t n = rg_scan_format_row(buf, sizeof(buf), 433920000, &ch);
    CHECK(n > 0, "row written");
    CHECK(strcmp(buf, "433920000,-97.3,-61.0,-98.0,3") == 0, "row content");
    char small[8];
    CHECK(rg_scan_format_row(small, sizeof(small), 433920000, &ch) == 0, "truncation reported");
    CHECK(small[0] == '\0', "truncated buffer left empty");
}

int main(void) {
    test_quiet_channel_no_activity();
    test_burst_detected_once();
    test_warmup_suppresses_detection();
    test_hysteresis();
    test_abs_min();
    test_floor_follows_slow_rise();
    test_reset_peak();
    test_global_floor();
    test_band_mask();
    test_format_row();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures == 0) printf("ALL SCAN TESTS PASSED\n");
    return g_failures ? 1 : 0;
}
