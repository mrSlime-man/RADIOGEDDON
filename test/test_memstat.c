/**
 * Host-side tests for the memory bookkeeping behind the diagnostics and the
 * radio-session memory check (helpers/rg_memstat.c). Values are synthetic.
 * Build & run via `make -C test check`.
 */
#include "../helpers/rg_memstat.h"
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        }                                                            \
    } while(0)

static void test_stat(void) {
    printf("test_stat\n");
    RgMemStat s;
    rg_memstat_init(&s, 80000);
    CHECK(rg_memstat_peak_use(&s) == 0, "nothing used at start");
    CHECK(strcmp(s.lowest_at, "start") == 0, "lowest is the start");
    rg_memstat_sample(&s, 60000, "radio");
    rg_memstat_sample(&s, 70000, "menu");
    CHECK(s.lowest_free == 60000 && strcmp(s.lowest_at, "radio") == 0, "lowest kept with place");
    CHECK(rg_memstat_peak_use(&s) == 20000, "peak use from the start");
    rg_memstat_sample(&s, 90000, "freed");
    CHECK(rg_memstat_peak_use(&s) == 20000, "a later rise does not hide the peak");
    rg_memstat_sample(&s, 60000, "again");
    CHECK(strcmp(s.lowest_at, "radio") == 0, "an equal low keeps the first place");
    rg_memstat_sample(&s, 1000, NULL);
    CHECK(strcmp(s.lowest_at, "?") == 0, "unnamed sample");
    CHECK(s.samples == 6, "samples counted");

    RgMemStat t;
    rg_memstat_init(&t, 1000);
    rg_memstat_sample(&t, 5000, "more");
    CHECK(rg_memstat_peak_use(&t) == 0, "more free than at start: no use");
}

static void test_session(void) {
    printf("test_session\n");
    CHECK(
        rg_mem_session_fits(RG_MEM_MIN_SESSION_COST, 0, 6000),
        "unmeasured: allowed above the floor");
    CHECK(!rg_mem_session_fits(5000, 0, 6000), "unmeasured: refused when no session could fit");
    CHECK(rg_mem_session_fits(30000, 20000, 6000), "fits with margin");
    CHECK(rg_mem_session_fits(26000, 20000, 6000), "exactly cost plus margin fits");
    CHECK(!rg_mem_session_fits(25999, 20000, 6000), "one byte short is refused");
    CHECK(!rg_mem_session_fits(0xFFFFFFFFu, 0xFFFFFFFFu, 1), "no overflow in the sum");

    CHECK(rg_mem_cost_changed(0, 15000), "first measurement is stored");
    CHECK(!rg_mem_cost_changed(15000, 0), "a missing measurement is ignored");
    CHECK(!rg_mem_cost_changed(15000, 15800), "small change ignored");
    CHECK(rg_mem_cost_changed(15000, 16100), "growth over 1 KB stored");
    CHECK(rg_mem_cost_changed(15000, 13900), "shrink over 1 KB stored");
}

static void test_cost_model(void) {
    printf("test_cost_model\n");
    const uint32_t binraw = 2048u * 4u + 2048u;
    // The owner's beta 12 measurement on RogueMaster 4f2e06200.
    CHECK(rg_mem_cost_for_model(36568, 0, binraw) == 36568 - binraw, "beta 12 cost less BinRAW");
    CHECK(rg_mem_cost_for_model(36568, 0, 0) == 36568, "nothing dropped: unchanged");
    CHECK(
        rg_mem_cost_for_model(36568, RG_MEM_SESSION_MODEL, binraw) == 36568,
        "current model: as measured");
    CHECK(rg_mem_cost_for_model(0, 0, binraw) == 0, "never measured stays unmeasured");
    // Never below the smallest session, never above what was stored.
    CHECK(
        rg_mem_cost_for_model(RG_MEM_MIN_SESSION_COST + 100, 0, binraw) == RG_MEM_MIN_SESSION_COST,
        "floored at the smallest session");
    CHECK(rg_mem_cost_for_model(5000, 0, binraw) == 5000, "a tiny stored cost is not raised");
    for(uint32_t stored = 1; stored < 80000; stored += 97) {
        uint32_t c = rg_mem_cost_for_model(stored, 0, binraw);
        CHECK(c <= stored, "never more than stored");
        CHECK(c + binraw >= stored, "never less than stored minus what was dropped");
    }
}

static void test_cost(void) {
    printf("test_cost\n");
    // Steady: low-water mark untouched (an earlier, deeper low exists).
    CHECK(rg_mem_session_cost(60000, 20000, 45000, 20000) == 15000, "steady cost");
    // Setup dipped below the old low-water mark: the dip counts.
    CHECK(rg_mem_session_cost(60000, 50000, 45000, 41000) == 19000, "transient peak counts");
    // A new low that is still above the steady use does not shrink the cost.
    CHECK(rg_mem_session_cost(60000, 46000, 45000, 45000) == 15000, "low at steady level");
    // Memory came back (another thread freed some): no negative cost.
    CHECK(rg_mem_session_cost(40000, 30000, 42000, 30000) == 0, "no underflow");
    CHECK(rg_mem_session_cost(0, 0, 0, 0) == 0, "zeros");
}

static void test_tag(void) {
    printf("test_tag\n");
    uint32_t a = rg_mem_firmware_tag("1.4.3", "a1b2c3d4");
    CHECK(a != 0, "tag non-zero");
    CHECK(a == rg_mem_firmware_tag("1.4.3", "a1b2c3d4"), "tag stable");
    CHECK(a != rg_mem_firmware_tag("1.4.3", "a1b2c3d5"), "hash changes the tag");
    CHECK(a != rg_mem_firmware_tag("1.4.4", "a1b2c3d4"), "version changes the tag");
    CHECK(rg_mem_firmware_tag("ab", "c") != rg_mem_firmware_tag("a", "bc"), "fields kept apart");
    CHECK(rg_mem_firmware_tag(NULL, NULL) != 0, "missing strings handled");
}

static void test_diag(void) {
    printf("test_diag\n");
    RgDiag d = {0};
    rg_diag_tick(&d, 1000, "start");
    CHECK(d.worst_gap_ms == 0 && d.stalls == 0, "first tick: no gap yet");
    for(uint32_t t = 1100; t <= 2000; t += 100)
        rg_diag_tick(&d, t, "idle");
    CHECK(d.worst_gap_ms == 100 && d.stalls == 0, "regular ticks: no stall");
    rg_diag_tick(&d, 2900, "Reading log");
    CHECK(d.worst_gap_ms == 900 && d.stalls == 1, "a 900 ms gap is a stall");
    CHECK(strcmp(d.worst_at, "Reading log") == 0, "and says where");
    rg_diag_tick(&d, 3000, NULL);
    CHECK(d.ticks == 13, "every tick counted");
    // The tick counter wraps (49 days of uptime) without a false stall.
    RgDiag w = {0};
    rg_diag_tick(&w, 0xFFFFFFC0u, "a");
    rg_diag_tick(&w, 0x24u, "b");
    CHECK(w.worst_gap_ms == 100 && w.stalls == 0, "a wrap is a 100 ms gap");

    CHECK(rg_mem_fragmentation_pct(0, 0) == 0, "no heap: 0");
    CHECK(rg_mem_fragmentation_pct(40000, 40000) == 0, "one block: 0%");
    CHECK(rg_mem_fragmentation_pct(40000, 10000) == 75, "a quarter in one block: 75%");
    CHECK(rg_mem_fragmentation_pct(40000, 50000) == 0, "inconsistent figures: 0");
    d.rec_bytes = 300000;
    d.rec_ms = 60000;
    CHECK(rg_diag_rec_rate(&d) == 5000, "5,000 B/s");
    d.rec_ms = 0;
    CHECK(rg_diag_rec_rate(&d) == 0, "nothing recorded: 0");
}

int main(void) {
    test_stat();
    test_session();
    test_cost();
    test_cost_model();
    test_tag();
    test_diag();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("MEMSTAT TESTS FAILED\n");
        return 1;
    }
    printf("ALL MEMSTAT TESTS PASSED\n");
    return 0;
}
