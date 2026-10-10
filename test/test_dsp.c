/**
 * Host-side unit tests for the firmware-independent DSP helpers.
 * Build & run:  cc -I.. -o test_dsp test_dsp.c ../helpers/radiogeddon_dsp.c && ./test_dsp
 * Or:           make -C test
 */
#include "../helpers/radiogeddon_dsp.h"
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

static void test_parse_basic(void) {
    printf("test_parse_basic\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    size_t n = radiogeddon_dsp_parse_line(
        "350 -350 700 -700 350", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 5, "parsed 5 samples");
    CHECK(count == 5, "count accumulated to 5");
    CHECK(min_us == 350, "min is 350");
    CHECK(max_us == 700, "max is 700");
}

static void test_parse_zero_skipped(void) {
    printf("test_parse_zero_skipped\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    size_t n =
        radiogeddon_dsp_parse_line("0 100 0 -200 0", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 2, "zero values skipped -> 2 samples");
    CHECK(min_us == 100, "min 100");
    CHECK(max_us == 200, "max 200 from abs(-200)");
}

static void test_parse_empty(void) {
    printf("test_parse_empty\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    size_t n = radiogeddon_dsp_parse_line("", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 0, "empty line -> 0 samples");
    CHECK(count == 0 && min_us == 0 && max_us == 0, "no stats changed");
}

static void test_clustering(void) {
    printf("test_clustering\n");
    RadioGeddonCluster clusters[RADIOGEDDON_MAX_CLUSTERS] = {0};
    size_t cn = 0;
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    // Two tight groups around ~350 and ~700 with small jitter.
    const char* line = "350 360 340 700 710 690 355 705";
    radiogeddon_dsp_parse_line(
        line, &count, &min_us, &max_us, clusters, &cn, RADIOGEDDON_MAX_CLUSTERS);
    CHECK(cn == 2, "two timing groups detected");
    radiogeddon_dsp_sort_clusters(clusters, cn);
    CHECK(clusters[0].center >= 340 && clusters[0].center <= 360, "cluster0 ~350");
    CHECK(clusters[1].center >= 690 && clusters[1].center <= 710, "cluster1 ~700");
    CHECK(clusters[0].count + clusters[1].count == 8, "all 8 samples clustered");
}

static void test_cluster_cap(void) {
    printf("test_cluster_cap\n");
    RadioGeddonCluster clusters[RADIOGEDDON_MAX_CLUSTERS] = {0};
    size_t cn = 0;
    // Many well-separated values: should cap at RADIOGEDDON_MAX_CLUSTERS.
    for(uint32_t v = 100; v <= 100 + 200 * 20; v += 200) {
        radiogeddon_dsp_cluster_add(
            clusters, &cn, RADIOGEDDON_MAX_CLUSTERS, v, RADIOGEDDON_CLUSTER_TOL);
    }
    CHECK(cn == RADIOGEDDON_MAX_CLUSTERS, "cluster count capped at max");
}

static void test_key_stats(void) {
    printf("test_key_stats\n");
    int nz = 0, dist = 0;
    radiogeddon_dsp_key_stats(0x0000000000000000ULL, &nz, &dist);
    CHECK(nz == 0, "all-zero key: 0 non-zero bytes");
    CHECK(dist == 1, "all-zero key: 1 distinct value (0x00)");

    radiogeddon_dsp_key_stats(0x0102030405060708ULL, &nz, &dist);
    CHECK(nz == 8, "sequential key: 8 non-zero bytes");
    CHECK(dist == 8, "sequential key: 8 distinct values");

    radiogeddon_dsp_key_stats(0xAAAAAAAAAAAAAAAAULL, &nz, &dist);
    CHECK(nz == 8, "repeated byte key: 8 non-zero");
    CHECK(dist == 1, "repeated byte key: 1 distinct value");
}

static void test_parse_whitespace(void) {
    printf("test_parse_whitespace\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    // Tabs, multiple spaces and leading/trailing space must all parse.
    size_t n = radiogeddon_dsp_parse_line(
        "  350\t-700   1050  ", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 3, "whitespace-separated -> 3 samples");
    CHECK(min_us == 350 && max_us == 1050, "min/max across whitespace");
}

static void test_parse_garbage_tail(void) {
    printf("test_parse_garbage_tail\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    // strtol stops at the first non-numeric token; we should keep what parsed.
    size_t n =
        radiogeddon_dsp_parse_line("350 -350 xyz 700", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 2, "parsing stops at non-numeric token");
    CHECK(max_us == 350, "only pre-garbage values counted");
}

static void test_parse_clamped_large(void) {
    printf("test_parse_clamped_large\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    // A very large but valid duration is taken as-is (abs value).
    size_t n =
        radiogeddon_dsp_parse_line("100 -2000000000", &count, &min_us, &max_us, NULL, NULL, 0);
    CHECK(n == 2, "two samples");
    CHECK(min_us == 100, "min 100");
    CHECK(max_us == 2000000000u, "large magnitude preserved");
}

// Values past int32 (found by fuzz/fuzz_db.c): strtol's overflow result used
// to be negated, which is undefined. They now saturate like the RAW reader.
static void test_parse_out_of_range(void) {
    printf("test_parse_out_of_range\n");
    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    size_t n = radiogeddon_dsp_parse_line(
        "-2147483648 99999999999999999999 -99999999999999999999 5",
        &count,
        &min_us,
        &max_us,
        NULL,
        NULL,
        0);
    CHECK(n == 4 && count == 4, "four samples");
    CHECK(max_us == 2147483647u, "saturated at INT32_MAX");
    CHECK(min_us == 5, "small value unaffected");
}

// Emit an int32 timing array as the RAW writer does
// (chunked, space-separated), then parse it back and verify no data loss.
static void test_raw_roundtrip(void) {
    printf("test_raw_roundtrip\n");
    enum {
        N = 1000,
        LINE = 512
    };
    static int32_t samples[N];
    for(int i = 0; i < N; i++) {
        int32_t mag = (i % 50 == 49) ? 8000 : 350; // periodic long gap
        samples[i] = (i & 1) ? -mag : mag;
    }

    size_t count = 0;
    uint32_t min_us = 0, max_us = 0;
    RadioGeddonCluster clusters[RADIOGEDDON_MAX_CLUSTERS] = {0};
    size_t cn = 0;

    char line[LINE * 12];
    int written = 0;
    while(written < N) {
        int chunk = (N - written) > LINE ? LINE : (N - written);
        int pos = 0;
        for(int i = 0; i < chunk; i++) {
            pos += snprintf(
                line + pos,
                sizeof(line) - pos,
                (i == 0) ? "%ld" : " %ld",
                (long)samples[written + i]);
        }
        radiogeddon_dsp_parse_line(
            line, &count, &min_us, &max_us, clusters, &cn, RADIOGEDDON_MAX_CLUSTERS);
        written += chunk;
    }

    CHECK(count == N, "round-trip preserves all samples");
    CHECK(min_us == 350, "round-trip min 350");
    CHECK(max_us == 8000, "round-trip max 8000 (gap)");
    CHECK(cn == 2, "round-trip yields two timing groups (350 and 8000)");
}

int main(void) {
    test_parse_basic();
    test_parse_zero_skipped();
    test_parse_empty();
    test_parse_whitespace();
    test_parse_garbage_tail();
    test_parse_clamped_large();
    test_parse_out_of_range();
    test_clustering();
    test_cluster_cap();
    test_raw_roundtrip();
    test_key_stats();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("TESTS FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
