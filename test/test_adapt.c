/**
 * Host tests for the Adaptive Scanner's scheduler (helpers/rg_adapt.c):
 * fairness bounds, the noisy-channel cap, dwell bounds, learning reset, and
 * Standard against the adaptive modes on identical simulated scenarios.
 * The scenarios are simulated transmissions (known start times and lengths)
 * and a simulated visit: a visit sees activity when its listening window
 * overlaps a transmission. They test the scheduling logic only; nothing
 * here says how many real transmissions a CC1101 would catch.
 * Run: make -C test
 */
#include "../helpers/rg_adapt.h"
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

#define RETUNE_MS 3u
#define CHANNELS  10u

/* A simulated transmitter: bursts of @p len ms every @p period ms on @p ch,
 * between @p from and @p until (ms). */
typedef struct {
    size_t ch;
    uint32_t period;
    uint32_t len;
    uint32_t phase;
    uint32_t from;
    uint32_t until;
} Tx;

static bool tx_on(const Tx* t, size_t n, size_t ch, uint32_t a, uint32_t b, uint32_t* burst) {
    for(size_t i = 0; i < n; i++) {
        if(t[i].ch != ch) continue;
        if(t[i].len >= t[i].period) {
            if(b > t[i].from && a < t[i].until) {
                if(burst) *burst = 0;
                return true;
            }
            continue;
        }
        // The bursts overlapping [a, b).
        uint32_t start = a > t[i].phase ? (a - t[i].phase) / t[i].period : 0;
        for(uint32_t k = start;; k++) {
            uint32_t s = t[i].phase + k * t[i].period;
            if(s >= b) break;
            if(s < t[i].from || s >= t[i].until) continue;
            if(s + t[i].len > a) {
                if(burst) *burst = s;
                return true;
            }
        }
    }
    return false;
}

typedef struct {
    uint32_t visits[CHANNELS];
    uint32_t caught[CHANNELS]; // distinct bursts seen
    uint32_t max_wait_picks[CHANNELS];
    uint32_t max_gap_ms;
    uint32_t visits_late[CHANNELS]; // in the second half
} Result;

static void
    simulate(RgAdaptMode mode, const Tx* tx, size_t ntx, uint32_t duration, Result* r, RgAdapt* a) {
    memset(r, 0, sizeof(*r));
    rg_adapt_init(a, mode, CHANNELS, 10);
    uint32_t now = 1;
    uint32_t last_pick[CHANNELS], last_burst[CHANNELS];
    bool prev[CHANNELS];
    for(size_t i = 0; i < CHANNELS; i++) {
        last_pick[i] = 0;
        last_burst[i] = UINT32_MAX;
        prev[i] = false;
    }
    uint32_t pick = 0;
    while(now < duration) {
        size_t ch = rg_adapt_next(a);
        uint16_t dwell = rg_adapt_dwell(a, ch);
        pick++;
        uint32_t wait = pick - last_pick[ch];
        if(last_pick[ch] && wait > r->max_wait_picks[ch]) r->max_wait_picks[ch] = wait;
        last_pick[ch] = pick;
        uint32_t a0 = now + RETUNE_MS, a1 = a0 + dwell;
        uint32_t burst = 0;
        bool on = tx_on(tx, ntx, ch, a0, a1, &burst);
        if(on && burst != last_burst[ch]) {
            r->caught[ch]++;
            last_burst[ch] = burst;
        }
        now = a1;
        rg_adapt_observe(a, ch, on, on && !prev[ch], on ? -50 : -100, dwell, now);
        prev[ch] = on;
        r->visits[ch]++;
        if(now > duration / 2) r->visits_late[ch]++;
    }
    r->max_gap_ms = rg_adapt_worst_gap(a, now);
}

static uint32_t total(const uint32_t* v) {
    uint32_t t = 0;
    for(size_t i = 0; i < CHANNELS; i++)
        t += v[i];
    return t;
}

static void test_standard_round_robin(void) {
    printf("test_standard_round_robin\n");
    RgAdapt a;
    rg_adapt_init(&a, RgAdaptStandard, 4, 10);
    size_t seq[8];
    for(size_t i = 0; i < 8; i++) {
        seq[i] = rg_adapt_next(&a);
        rg_adapt_observe(&a, seq[i], i == 1, i == 1, -60, 10, (uint32_t)(i + 1) * 13);
    }
    CHECK(seq[0] == 0 && seq[1] == 1 && seq[3] == 3 && seq[4] == 0 && seq[7] == 3, "in order");
    CHECK(rg_adapt_dwell(&a, 1) == 10, "fixed dwell even after activity");
}

static void test_fairness(void) {
    printf("test_fairness\n");
    // One channel very busy, one a constant carrier, the rest quiet.
    Tx tx[] = {
        {3, 400, 60, 0, 0, UINT32_MAX},
        {7, 1, 1, 0, 0, UINT32_MAX},
    };
    for(int mode = RgAdaptBalanced; mode < RgAdaptModeCount; mode++) {
        RgAdapt a;
        Result r;
        simulate((RgAdaptMode)mode, tx, 2, 120000, &r, &a);
        bool fair = true;
        for(size_t i = 0; i < CHANNELS; i++)
            if(r.max_wait_picks[i] > a.max_revisit + 1u || r.visits[i] == 0) fair = false;
        CHECK(fair, "no channel waits beyond the revisit bound");
        CHECK(rg_adapt_noisy(&a.ch[7]), "the carrier is recognised as noisy");
        CHECK(r.visits[7] * 100u / total(r.visits) < 25u, "the carrier cannot take the scan");
        CHECK(
            r.max_gap_ms <= (uint32_t)a.max_revisit * (a.max_dwell_ms + RETUNE_MS) + 50u,
            "longest unobserved gap is bounded in time");
        CHECK(r.visits[3] > r.visits[0], "the busy channel is visited more");
        bool dwell_ok = true;
        for(size_t i = 0; i < CHANNELS; i++) {
            uint16_t d = rg_adapt_dwell(&a, i);
            if(d < a.min_dwell_ms || d > a.max_dwell_ms) dwell_ok = false;
        }
        CHECK(dwell_ok, "dwells stay within bounds");
    }
}

static void test_standard_vs_adaptive(void) {
    printf("test_standard_vs_adaptive\n");
    // Short bursts (25 ms every 1.5 s and every 2.3 s) on two channels of
    // ten. Whether one schedule meets a strictly periodic burst is a matter
    // of phase, so 24 phase offsets are run and the totals compared.
    uint32_t seen[RgAdaptModeCount] = {0};
    uint32_t sent = 0;
    bool every_channel = true;
    for(uint32_t ph = 0; ph < 24; ph++) {
        Tx tx[] = {
            {2, 1500, 25, 37 * ph, 0, UINT32_MAX},
            {6, 2300, 25, 700 + 91 * ph, 0, UINT32_MAX},
        };
        sent += 120000 / 1500 + 120000 / 2300;
        for(int mode = 0; mode < RgAdaptCustom; mode++) {
            Result r;
            RgAdapt a;
            simulate((RgAdaptMode)mode, tx, 2, 120000, &r, &a);
            seen[mode] += r.caught[2] + r.caught[6];
            for(size_t i = 0; i < CHANNELS; i++)
                if(r.visits[i] == 0) every_channel = false;
        }
    }
    printf(
        "  bursts seen of %u sent: Standard %u, Balanced %u, Focused %u\n",
        (unsigned)sent,
        (unsigned)seen[RgAdaptStandard],
        (unsigned)seen[RgAdaptBalanced],
        (unsigned)seen[RgAdaptFocused]);
    CHECK(seen[RgAdaptBalanced] > seen[RgAdaptStandard], "Balanced sees more than Standard");
    CHECK(seen[RgAdaptFocused] > seen[RgAdaptBalanced], "Focused sees more than Balanced");
    CHECK(every_channel, "every mode still visits every channel");
}

static void test_changing_signals(void) {
    printf("test_changing_signals\n");
    // Activity on channel 1 for the first minute, then on channel 8.
    Tx tx[] = {
        {1, 800, 60, 0, 0, 60000},
        {8, 800, 60, 0, 60000, 120000},
    };
    Result r;
    RgAdapt a;
    simulate(RgAdaptBalanced, tx, 2, 120000, &r, &a);
    uint32_t early1 = r.visits[1] - r.visits_late[1];
    uint32_t early8 = r.visits[8] - r.visits_late[8];
    CHECK(early1 > early8, "first half favours channel 1");
    CHECK(r.visits_late[8] > r.visits_late[1], "second half moves to channel 8");
}

static void test_reset_and_priority(void) {
    printf("test_reset_and_priority\n");
    RgAdapt a;
    rg_adapt_init(&a, RgAdaptCustom, 5, 20);
    rg_adapt_set_priority(&a, 4, 3);
    rg_adapt_set_priority(&a, 2, 9);
    CHECK(a.ch[2].priority == 3, "priority clamped");
    uint32_t v[5] = {0};
    for(uint32_t i = 0; i < 500; i++) {
        size_t c = rg_adapt_next(&a);
        v[c]++;
        rg_adapt_observe(&a, c, false, false, -100, 20, i * 23 + 1);
    }
    CHECK(v[4] > v[0] && v[2] > v[0], "priorities get more visits");
    CHECK(v[0] > 0, "no starvation of priority 0");
    rg_adapt_observe(&a, 1, true, true, -40, 20, 99999);
    CHECK(a.ch[1].hits == 1 && a.ch[1].peak_dbm == -40, "observed");
    rg_adapt_reset(&a);
    CHECK(a.ch[1].hits == 0 && a.ch[1].visits == 0 && a.ch[1].peak_dbm == -127, "reset");
    CHECK(a.ch[4].priority == 3, "priorities survive a reset");
    CHECK(a.picks == 0, "picks reset");
    // Degenerate configurations.
    rg_adapt_init(&a, RgAdaptFocused, 1, 0);
    CHECK(rg_adapt_next(&a) == 0 && rg_adapt_next(&a) == 0, "a single channel always");
    rg_adapt_init(&a, RgAdaptBalanced, 0, 10);
    CHECK(rg_adapt_next(&a) == 0, "no channels: no crash");
    rg_adapt_init(&a, (RgAdaptMode)77, 100, 10);
    CHECK(a.mode == RgAdaptStandard && a.count == RG_ADAPT_MAX_CHANNELS, "clamped");
    CHECK(rg_adapt_share(&a, 99) == 0, "share out of range");
    CHECK(strcmp(rg_adapt_mode_name(RgAdaptFocused), "Focused") == 0, "name");
}

int main(void) {
    test_standard_round_robin();
    test_fairness();
    test_standard_vs_adaptive();
    test_changing_signals();
    test_reset_and_priority();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
