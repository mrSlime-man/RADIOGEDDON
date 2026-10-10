/**
 * Engine lifecycle tests: the real Scanner, Range Scanner and Hopper engines
 * (helpers/radiogeddon_scanner.c, _rangescan.c, _hopper.c) on real threads,
 * against the fake radio in stubs/radio_fake.c and Furi stand-ins that count
 * every allocation (STUB_TRACK_ALLOC).
 *
 * Each engine is started and stopped many times in a row, as a user going in
 * and out of its screen does, and must:
 * - leave no allocation behind once freed (the out-of-memory crash of
 *   1.0.0-beta.1 was memory that should not have been held);
 * - keep its memory the same from one start to the next (no growth);
 * - close every radio session and every capture it opened;
 * - never ask the radio for a frequency outside the bands (Range Scanner);
 * - still detect activity where the fake radio puts a signal.
 *
 * Build & run: make -C test lifecycle (needs the firmware's Sub-GHz headers,
 * fetched like the format tests).
 */
#include "../helpers/radiogeddon_scanner.h"
#include "../helpers/radiogeddon_hopper.h"
#include "../helpers/radiogeddon_rangescan.h"

#include <stdatomic.h>
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

#define CYCLES 40

extern size_t stub_live_bytes;
extern size_t stub_live_blocks;
extern _Atomic uint32_t fake_active_hz;
extern _Atomic int fake_scan_sessions;
extern _Atomic int fake_scan_begins;
extern _Atomic int fake_probes;
extern _Atomic int fake_bad_probes;
extern _Atomic int fake_records_open;
extern _Atomic int fake_records_started;
extern _Atomic int fake_records_saved;
extern uint32_t fake_band_lo, fake_band_hi;

static RadioGeddonSubGhz* const fake_radio = (RadioGeddonSubGhz*)&g_checks; // never dereferenced

static _Atomic int g_hits;
static _Atomic uint32_t g_last_hit;

static void scanner_hit(size_t index, void* context) {
    (void)context;
    atomic_fetch_add(&g_hits, 1);
    atomic_store(&g_last_hit, (uint32_t)index);
}

static void range_hit(uint32_t index, void* context) {
    scanner_hit(index, context);
}

static void test_scanner_cycles(void) {
    printf("test_scanner_cycles\n");
    size_t base_bytes = stub_live_bytes, base_blocks = stub_live_blocks;
    uint32_t list[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    for(size_t i = 0; i < RADIOGEDDON_SCANNER_MAX_CHANNELS; i++)
        list[i] = 433000000 + i * 25000;

    RadioGeddonScanner* sc = radiogeddon_scanner_alloc(fake_radio);
    radiogeddon_scanner_configure(sc, list, RADIOGEDDON_SCANNER_MAX_CHANNELS, 1, 10, false);
    radiogeddon_scanner_set_callback(sc, scanner_hit, NULL);
    size_t held = stub_live_bytes;
    atomic_store(&fake_active_hz, 0);
    atomic_store(&g_hits, 0);
    int begins = atomic_load(&fake_scan_begins);
    for(int c = 0; c < CYCLES; c++) {
        radiogeddon_scanner_start(sc);
        furi_delay_ms(3);
        radiogeddon_scanner_stop(sc);
        if(stub_live_bytes != held) {
            CHECK(false, "scanner memory unchanged between start/stop cycles");
            break;
        }
    }
    CHECK(atomic_load(&fake_scan_sessions) == 0, "every scan session closed");
    CHECK(atomic_load(&fake_scan_begins) - begins == CYCLES, "one session per start");

    // A signal on channel 7 is detected (after the noise-floor warm-up).
    atomic_store(&fake_active_hz, list[7]);
    radiogeddon_scanner_start(sc);
    for(int t = 0; t < 400 && atomic_load(&g_hits) == 0; t++)
        furi_delay_ms(2);
    radiogeddon_scanner_stop(sc);
    atomic_store(&fake_active_hz, 0);
    CHECK(atomic_load(&g_hits) >= 1 && atomic_load(&g_last_hit) == 7, "activity on channel 7");

    RadioGeddonScannerSnapshot* snap = malloc(sizeof(RadioGeddonScannerSnapshot));
    radiogeddon_scanner_snapshot(sc, snap);
    CHECK(snap->count == RADIOGEDDON_SCANNER_MAX_CHANNELS, "snapshot holds the list");
    CHECK(snap->channels[7].hits >= 1 && snap->channels[7].peak > -50.0f, "peak held");
    free(snap);

    radiogeddon_scanner_free(sc);
    CHECK(stub_live_bytes == base_bytes && stub_live_blocks == base_blocks, "scanner freed fully");
}

static void test_rangescan_cycles(void) {
    printf("test_rangescan_cycles\n");
    size_t base_bytes = stub_live_bytes, base_blocks = stub_live_blocks;
    // The full 256 points over two bands with a gap the radio rejects.
    RgBandSet bands = {.band = {{300000000, 348000000}, {387000000, 464000000}}, .count = 2};
    fake_band_lo = 300000000;
    fake_band_hi = 464000000;
    RgRange range;
    CHECK(
        rg_range_plan(&range, 340000000, 400000000, 200000, &bands, RG_RANGE_MAX_POINTS) ==
            RgRangeOk,
        "plan across the gap");
    // 340.0..348.0 (41 points) and 387.0..400.0 (66 points).
    CHECK(range.points == 41 + 66, "points either side of the gap");
    uint32_t active = rg_range_frequency(&range, 50);

    for(int c = 0; c < CYCLES; c++) {
        size_t before = stub_live_bytes;
        RadioGeddonRangeScan* rs = radiogeddon_rangescan_alloc(fake_radio, &range, 1, 10, false);
        size_t engine = stub_live_bytes - before;
        if(c == 0) {
            // The memory figure the setup screen checks before allocating.
            CHECK(engine <= radiogeddon_rangescan_memory(range.points) + 64, "memory as declared");
        }
        radiogeddon_rangescan_start(rs);
        furi_delay_ms(2);
        radiogeddon_rangescan_stop(rs);
        radiogeddon_rangescan_free(rs);
        if(stub_live_bytes != before) {
            CHECK(false, "range scan memory returned after each cycle");
            break;
        }
    }
    CHECK(atomic_load(&fake_scan_sessions) == 0, "every range session closed");

    // Detection, peak, hold on hit, recalibration and the gap never probed.
    RadioGeddonRangeScan* rs = radiogeddon_rangescan_alloc(fake_radio, &range, 1, 10, true);
    atomic_store(&g_hits, 0);
    radiogeddon_rangescan_set_callback(rs, range_hit, NULL);
    atomic_store(&fake_bad_probes, 0);
    RadioGeddonRangeSnapshot* snap = malloc(sizeof(RadioGeddonRangeSnapshot));
    // Quiet air first: a carrier present from the start would only become
    // that point's noise floor (rg_scan), not activity.
    radiogeddon_rangescan_start(rs);
    for(int t = 0; t < 1000; t++) {
        radiogeddon_rangescan_snapshot(rs, snap);
        if(!snap->calibrating) break;
        furi_delay_ms(2);
    }
    CHECK(!snap->calibrating, "calibrated on quiet air");
    atomic_store(&fake_active_hz, active);
    for(int t = 0; t < 1000 && atomic_load(&g_hits) == 0; t++)
        furi_delay_ms(2);
    furi_delay_ms(20); // keep holding for a while
    radiogeddon_rangescan_stop(rs);
    CHECK(atomic_load(&g_hits) >= 1 && atomic_load(&g_last_hit) == 50, "hit at point 50");
    CHECK(atomic_load(&fake_bad_probes) == 0, "no probe inside the gap");

    radiogeddon_rangescan_snapshot(rs, snap);
    CHECK(snap->hold_index == 50, "holding on the active point");
    CHECK(snap->point[50].peak > -50 && snap->point[50].hits >= 1, "peak and counter at 50");
    CHECK(snap->floor != RG_SPECTRUM_NONE && snap->floor < -90, "noise floor from quiet points");
    radiogeddon_rangescan_recalibrate(rs);
    radiogeddon_rangescan_snapshot(rs, snap);
    CHECK(snap->calibrating && snap->hold_index == -1 && snap->sweeps == 0, "recalibrated");
    free(snap);
    atomic_store(&fake_active_hz, 0);
    radiogeddon_rangescan_free(rs);
    CHECK(
        stub_live_bytes == base_bytes && stub_live_blocks == base_blocks,
        "range scan freed fully");
    fake_band_lo = 300000000;
    fake_band_hi = 928000000;
}

static _Atomic int g_saved_events;
static void hopper_event(RadioGeddonHopperEvent event, void* context) {
    (void)context;
    if(event == RadioGeddonHopperEventRecordSaved) atomic_fetch_add(&g_saved_events, 1);
}

static void test_hopper_cycles(void) {
    printf("test_hopper_cycles\n");
    size_t base_bytes = stub_live_bytes, base_blocks = stub_live_blocks;
    uint32_t list[4] = {315000000, 390000000, 433920000, 868350000};
    RadioGeddonHopper* hop = radiogeddon_hopper_alloc(fake_radio);
    radiogeddon_hopper_configure(hop, list, 4, 50, 100, 10, true);
    radiogeddon_hopper_set_callback(hop, hopper_event, NULL);
    size_t held = stub_live_bytes;
    for(int c = 0; c < CYCLES / 4; c++) {
        radiogeddon_hopper_start(hop);
        furi_delay_ms(15);
        radiogeddon_hopper_stop(hop);
        if(stub_live_bytes != held) {
            CHECK(false, "hopper memory unchanged between cycles");
            break;
        }
    }

    // Activity on 433.92 with auto-record: a capture is made and saved, and
    // stopping in the middle of one saves it too.
    atomic_store(&g_saved_events, 0);
    int started = atomic_load(&fake_records_started);
    radiogeddon_hopper_start(hop);
    furi_delay_ms(600); // every channel visited on quiet air: floors known
    atomic_store(&fake_active_hz, 433920000);
    for(int t = 0; t < 400 && atomic_load(&fake_records_started) == started; t++)
        furi_delay_ms(5);
    radiogeddon_hopper_stop(hop);
    atomic_store(&fake_active_hz, 0);
    CHECK(atomic_load(&fake_records_started) > started, "auto-record started on activity");
    CHECK(atomic_load(&fake_records_open) == 0, "every capture saved or discarded");
    CHECK(atomic_load(&g_saved_events) >= 1, "capture saved when the hopper stopped");

    RadioGeddonHopperStatus st;
    radiogeddon_hopper_status(hop, &st);
    CHECK(!st.recording, "not recording once stopped");
    radiogeddon_hopper_free(hop);
    CHECK(stub_live_bytes == base_bytes && stub_live_blocks == base_blocks, "hopper freed fully");
}

int main(void) {
    test_scanner_cycles();
    test_rangescan_cycles();
    test_hopper_cycles();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
