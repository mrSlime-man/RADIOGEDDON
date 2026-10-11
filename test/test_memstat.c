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

int main(void) {
    test_stat();
    test_session();
    test_cost();
    test_tag();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("MEMSTAT TESTS FAILED\n");
        return 1;
    }
    printf("ALL MEMSTAT TESTS PASSED\n");
    return 0;
}
