/**
 * Host-side unit tests for the Frequency Hopper logic (helpers/rg_hop.c).
 * Inputs are synthetic RSSI sequences and timestamps, not radio measurements.
 * Run: make -C test
 */
#include "../helpers/rg_hop.h"
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

#define SAMPLE_MS 10u

/* Simulated radio: per-channel RSSI as a function of time. */
typedef float (*RssiFn)(size_t channel, uint32_t now_ms);

typedef struct {
    uint32_t retunes;
    uint32_t starts;
    uint32_t ends;
    uint32_t time_on[RG_HOP_MAX_CHANNELS];
} SimResult;

static void simulate(
    RgHop* hop,
    const RgHopConfig* cfg,
    RssiFn fn,
    uint32_t from,
    uint32_t to,
    SimResult* r) {
    for(uint32_t t = from; t < to; t += SAMPLE_MS) {
        r->time_on[hop->current] += SAMPLE_MS;
        RgHopStep s = rg_hop_feed(hop, cfg, fn(hop->current, t), t);
        if(s.activity_started) r->starts++;
        if(s.activity_ended) r->ends++;
        if(s.retune) {
            r->retunes++;
            rg_hop_tuned(hop, s.next_channel, t);
        }
    }
}

static float quiet(size_t channel, uint32_t now_ms) {
    (void)now_ms;
    return -100.0f + (float)(channel % 3); // slightly different floors
}

static void test_cycles_when_quiet(void) {
    printf("test_cycles_when_quiet\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    RgHop hop;
    rg_hop_init(&hop, 4, 0);
    SimResult r = {0};
    simulate(&hop, &cfg, quiet, 0, 4000, &r);
    CHECK(r.starts == 0, "no activity on quiet bands");
    CHECK(r.retunes >= 18 && r.retunes <= 20, "about one retune per 200 ms dwell");
    for(int i = 0; i < 4; i++)
        CHECK(r.time_on[i] >= 900 && r.time_on[i] <= 1100, "time shared evenly between channels");
}

/* A transmission on channel 2 between 1.0 s and 1.6 s. */
static float burst_on_2(size_t channel, uint32_t now_ms) {
    if(channel == 2 && now_ms >= 1000 && now_ms < 1600) return -55.0f;
    return -100.0f;
}

static void test_holds_on_activity(void) {
    printf("test_holds_on_activity\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg); // 200 ms dwell, 2 s hold
    RgHop hop;
    rg_hop_init(&hop, 4, 0);
    SimResult r = {0};
    // Run until the hopper first lands on channel 2 during the burst.
    simulate(&hop, &cfg, burst_on_2, 0, 1700, &r);
    CHECK(r.starts == 1, "burst detected once");
    CHECK(hop.holding, "holding after the burst");
    CHECK(hop.current == 2, "held on the active channel");
    uint32_t retunes_before = r.retunes;
    // Hold lasts 2 s after the signal was last above threshold (until ~3.6 s).
    simulate(&hop, &cfg, burst_on_2, 1700, 3500, &r);
    CHECK(r.retunes == retunes_before, "no retune while holding");
    simulate(&hop, &cfg, burst_on_2, 3500, 3800, &r);
    CHECK(r.ends == 1, "hold ends after the hold time");
    CHECK(!hop.holding, "no longer holding");
    const RgHopEvent* e = rg_hop_event(&hop, 0);
    CHECK(e != NULL && e->channel == 2, "event recorded for channel 2");
    CHECK(e && !e->open, "event closed");
    CHECK(e && e->peak_dbm == -55.0f, "event peak is the burst level");
    CHECK(e && e->duration_ms >= 2000, "event covers the hold");
    CHECK(hop.stats[2].active_ms >= 2000, "active time accumulated");
    simulate(&hop, &cfg, burst_on_2, 3800, 4500, &r);
    CHECK(r.retunes > retunes_before, "hopping resumes after the hold");
}

static void test_lock(void) {
    printf("test_lock\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    RgHop hop;
    rg_hop_init(&hop, 4, 0);
    rg_hop_set_locked(&hop, true, 0);
    SimResult r = {0};
    simulate(&hop, &cfg, quiet, 0, 3000, &r);
    CHECK(r.retunes == 0, "no retunes while locked");
    rg_hop_set_locked(&hop, false, 3000);
    simulate(&hop, &cfg, quiet, 3000, 3150, &r);
    CHECK(r.retunes == 0, "unlocking gives the channel a fresh dwell");
    simulate(&hop, &cfg, quiet, 3150, 3300, &r);
    CHECK(r.retunes == 1, "then hopping resumes");
}

static void test_decode_extends_hold(void) {
    printf("test_decode_extends_hold\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    RgHop hop;
    rg_hop_init(&hop, 3, 0);
    SimResult r = {0};
    simulate(&hop, &cfg, quiet, 0, 100, &r);
    // A decode with no RSSI rise (weak but decodable signal) still holds.
    CHECK(rg_hop_note_decode(&hop, &cfg, "Princeton", 100), "decode starts activity");
    CHECK(hop.holding, "holding after decode");
    simulate(&hop, &cfg, quiet, 100, 1500, &r);
    CHECK(r.retunes == 0, "still holding 1.4 s later");
    CHECK(!rg_hop_note_decode(&hop, &cfg, "Other", 1500), "second decode extends, not starts");
    simulate(&hop, &cfg, quiet, 1500, 3400, &r);
    CHECK(r.retunes == 0, "extended hold still in force");
    simulate(&hop, &cfg, quiet, 3400, 3800, &r);
    CHECK(r.ends == 1 && r.retunes >= 1, "hold ends, hopping resumes");
    const RgHopEvent* e = rg_hop_event(&hop, 0);
    CHECK(e && strcmp(e->protocol, "Princeton") == 0, "first protocol kept");
    CHECK(e && e->decodes == 2, "decodes counted on the event");
    CHECK(hop.stats[0].decodes == 2, "decodes counted on the channel");
}

static void test_event_ring(void) {
    printf("test_event_ring\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    cfg.hold_ms = 10;
    RgHop hop;
    rg_hop_init(&hop, 1, 0);
    uint32_t t = 0;
    for(int i = 0; i < RG_HOP_MAX_EVENTS + 5; i++) {
        rg_hop_note_decode(&hop, &cfg, "P", t);
        t += 100;
        rg_hop_feed(&hop, &cfg, -100.0f, t); // hold expires
        t += 10;
    }
    CHECK(hop.event_count == RG_HOP_MAX_EVENTS, "ring keeps the newest events only");
    const RgHopEvent* newest = rg_hop_event(&hop, 0);
    const RgHopEvent* older = rg_hop_event(&hop, 1);
    CHECK(newest && older && newest->start_ms > older->start_ms, "newest first");
    CHECK(rg_hop_event(&hop, RG_HOP_MAX_EVENTS) == NULL, "out-of-range event is NULL");
    rg_hop_mark_recorded(&hop);
    CHECK(rg_hop_event(&hop, 0)->recorded, "mark_recorded marks the newest");
}

static void test_single_and_empty(void) {
    printf("test_single_and_empty\n");
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    RgHop hop;
    rg_hop_init(&hop, 1, 0);
    SimResult r = {0};
    simulate(&hop, &cfg, quiet, 0, 2000, &r);
    CHECK(r.retunes == 0, "one channel never retunes");
    rg_hop_init(&hop, 0, 0);
    RgHopStep s = rg_hop_feed(&hop, &cfg, -50.0f, 10);
    CHECK(!s.retune && !s.activity_started, "empty list is inert");
    CHECK(!rg_hop_note_decode(&hop, &cfg, "P", 10), "decode on empty list ignored");
    rg_hop_init(&hop, 100, 0);
    CHECK(hop.count == RG_HOP_MAX_CHANNELS, "channel count clamped");
}

static void test_repeated_cycles_stable(void) {
    printf("test_repeated_cycles_stable\n");
    // Many hold/release/retune cycles: state must stay consistent.
    RgHopConfig cfg;
    rg_hop_config_default(&cfg);
    cfg.hold_ms = 300;
    RgHop hop;
    rg_hop_init(&hop, 4, 0);
    SimResult r = {0};
    uint32_t t = 0;
    for(int cycle = 0; cycle < 200; cycle++) {
        simulate(&hop, &cfg, quiet, t, t + 500, &r);
        t += 500;
        if(rg_hop_note_decode(&hop, &cfg, "P", t)) r.starts++;
        rg_hop_set_locked(&hop, (cycle % 7) == 0, t);
        rg_hop_set_locked(&hop, false, t);
    }
    simulate(&hop, &cfg, quiet, t, t + 1000, &r);
    CHECK(hop.current < hop.count, "current channel valid");
    CHECK(!hop.holding, "no stuck hold");
    CHECK(r.starts == r.ends || r.starts == r.ends + 1, "every hold opened is closed");
    CHECK(hop.event_count == RG_HOP_MAX_EVENTS, "ring full but bounded");
}

int main(void) {
    test_cycles_when_quiet();
    test_holds_on_activity();
    test_lock();
    test_decode_extends_hold();
    test_event_ring();
    test_single_and_empty();
    test_repeated_cycles_stable();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures == 0) printf("ALL HOP TESTS PASSED\n");
    return g_failures ? 1 : 0;
}
