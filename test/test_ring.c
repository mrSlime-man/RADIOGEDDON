/**
 * Host-side tests for the lock-free SPSC sample ring (helpers/rg_ring.c).
 * Build & run via `make -C test check`.
 *
 * The stress test runs a real producer thread against a consumer that stalls
 * now and then, standing in for the radio worker and a slow SD card. The
 * values are a synthetic counter, not radio data.
 */
#define _POSIX_C_SOURCE 200809L
#include "../helpers/rg_ring.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void test_init(void) {
    printf("test_init\n");
    static int32_t store[16];
    RgRing r;
    CHECK(!rg_ring_init(&r, store, 0), "capacity 0 refused");
    CHECK(!rg_ring_init(&r, store, 1), "capacity 1 refused");
    CHECK(!rg_ring_init(&r, store, 12), "non power of two refused");
    CHECK(!rg_ring_init(&r, NULL, 16), "no storage refused");
    CHECK(rg_ring_init(&r, store, 16), "power of two accepted");
    CHECK(rg_ring_capacity(&r) == 16 && rg_ring_used(&r) == 0, "empty ring of 16");
}

static void test_fifo_and_full(void) {
    printf("test_fifo_and_full\n");
    static int32_t store[8];
    RgRing r;
    rg_ring_init(&r, store, 8);
    for(int32_t i = 1; i <= 8; i++)
        CHECK(rg_ring_push(&r, i % 2 ? i : -i), "push into free slot");
    CHECK(rg_ring_used(&r) == 8, "ring full");
    CHECK(!rg_ring_push(&r, 9), "push into full ring refused");
    CHECK(!rg_ring_push(&r, -10), "second refusal");

    RgRingStats st;
    rg_ring_stats(&r, &st);
    CHECK(st.pushed == 8 && st.dropped == 2, "8 accepted, 2 lost");
    CHECK(st.gaps == 1 && st.first_gap == 8, "one gap, after sample 8");
    CHECK(st.peak == 8, "peak fill 8");

    int32_t out[8];
    CHECK(rg_ring_pop(&r, out, 3) == 3, "pop three");
    CHECK(out[0] == 1 && out[1] == -2 && out[2] == 3, "oldest first, signs kept");
    CHECK(rg_ring_push(&r, 11), "room again after popping");
    CHECK(rg_ring_push(&r, 12) && rg_ring_push(&r, 13), "fill up again");
    CHECK(!rg_ring_push(&r, 14), "full again");
    rg_ring_stats(&r, &st);
    CHECK(st.gaps == 2 && st.dropped == 3, "a new run of losses is a new gap");
    CHECK(st.first_gap == 8, "first gap position kept");

    size_t n = rg_ring_pop(&r, out, 8);
    CHECK(n == 8, "pop the rest");
    const int32_t expect[] = {-4, 5, -6, 7, -8, 11, 12, 13};
    CHECK(memcmp(out, expect, sizeof(expect)) == 0, "order across the refusal");
    CHECK(rg_ring_pop(&r, out, 8) == 0, "empty");
}

static void test_wraparound(void) {
    printf("test_wraparound\n");
    static int32_t store[4];
    RgRing r;
    rg_ring_init(&r, store, 4);
    // Start just below the 32-bit wrap of the free-running counters.
    r.head = r.tail = 0xFFFFFFFEu;
    int32_t next = 1, want = 1;
    int32_t out[3];
    bool ok = true;
    for(int round = 0; round < 10; round++) {
        for(int i = 0; i < 3; i++)
            ok = ok && rg_ring_push(&r, next++);
        ok = ok && rg_ring_used(&r) == 3;
        size_t n = rg_ring_pop(&r, out, 3);
        ok = ok && n == 3;
        for(size_t i = 0; i < n; i++)
            ok = ok && out[i] == want++;
    }
    CHECK(ok, "order and counts hold across the counter wrap");
    CHECK(r.head == r.tail && r.head < 0xFFFFFFFEu, "counters wrapped");
}

static void test_capacity_for(void) {
    printf("test_capacity_for\n");
    CHECK(rg_ring_capacity_for(100000, 1024, 8192) == 8192, "capped at max");
    CHECK(rg_ring_capacity_for(20000, 1024, 8192) == 4096, "largest power of two that fits");
    CHECK(rg_ring_capacity_for(4096, 1024, 8192) == 1024, "exactly the minimum");
    CHECK(rg_ring_capacity_for(4000, 1024, 8192) == 0, "below the minimum: refused");
    CHECK(rg_ring_capacity_for(0, 1024, 8192) == 0, "no memory");
}

static void test_sample(void) {
    printf("test_sample\n");
    CHECK(rg_ring_sample(true, 400) == 400, "high is positive");
    CHECK(rg_ring_sample(false, 1200) == -1200, "low is negative");
    CHECK(rg_ring_sample(true, 0xFFFFFFFFu) == INT32_MAX, "clamped high");
    CHECK(rg_ring_sample(false, 0x80000000u) == -INT32_MAX, "clamped low");
}

/* ---- Threaded stress ---------------------------------------------------- */

#define STRESS_N   2000000
#define STRESS_CAP 1024

typedef struct {
    RgRing ring;
    int32_t store[STRESS_CAP];
    volatile int done;
} Stress;

static void* stress_producer(void* arg) {
    Stress* s = arg;
    for(int32_t i = 1; i <= STRESS_N; i++)
        rg_ring_push(&s->ring, i & 1 ? i : -i);
    __atomic_store_n(&s->done, 1, __ATOMIC_RELEASE);
    return NULL;
}

static void test_threaded(void) {
    printf("test_threaded\n");
    static Stress s;
    rg_ring_init(&s.ring, s.store, STRESS_CAP);
    s.done = 0;
    pthread_t t;
    pthread_create(&t, NULL, stress_producer, &s);

    int32_t out[96];
    uint64_t popped = 0;
    int32_t last = 0;
    bool ordered = true, signs = true;
    unsigned iter = 0;
    while(true) {
        int finished = __atomic_load_n(&s.done, __ATOMIC_ACQUIRE);
        size_t n = rg_ring_pop(&s.ring, out, sizeof(out) / sizeof(out[0]));
        for(size_t i = 0; i < n; i++) {
            int32_t mag = out[i] < 0 ? -out[i] : out[i];
            if(mag <= last || mag > STRESS_N) ordered = false;
            if((mag & 1) != (out[i] > 0)) signs = false;
            last = mag;
        }
        popped += n;
        if(n == 0 && finished) break;
        // Stall now and then, like an SD card finishing a cluster.
        if(++iter % 512 == 0) {
            struct timespec ts = {0, 200000};
            nanosleep(&ts, NULL);
        }
    }
    pthread_join(t, NULL);

    RgRingStats st;
    rg_ring_stats(&s.ring, &st);
    CHECK(ordered, "values arrive in order, none duplicated or torn");
    CHECK(signs, "levels arrive intact");
    CHECK(popped == st.pushed, "everything accepted is delivered");
    CHECK(st.pushed + st.dropped == STRESS_N, "every push is accepted or counted lost");
    CHECK(st.peak <= STRESS_CAP, "fill never exceeds capacity");
    CHECK(st.dropped == 0 || st.gaps > 0, "losses are recorded as gaps");
    printf(
        "  (%lu delivered, %lu lost in %lu gaps, peak %lu)\n",
        (unsigned long)st.pushed,
        (unsigned long)st.dropped,
        (unsigned long)st.gaps,
        (unsigned long)st.peak);
}

int main(void) {
    test_init();
    test_fifo_and_full();
    test_wraparound();
    test_capacity_for();
    test_sample();
    test_threaded();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("RING TESTS FAILED\n");
        return 1;
    }
    printf("ALL RING TESTS PASSED\n");
    return 0;
}
