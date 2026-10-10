/**
 * Host-side unit tests for frequency text and range checks (helpers/rg_freq.c).
 * Run: make -C test
 */
#include "../helpers/rg_freq.h"
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

static void check_text(uint32_t hz, const char* want) {
    char out[RG_FREQ_TEXT_SIZE];
    rg_freq_text(hz, out, sizeof(out));
    if(strcmp(out, want) != 0)
        printf("  %lu Hz -> \"%s\", want \"%s\"\n", (unsigned long)hz, out, want);
    CHECK(strcmp(out, want) == 0, "frequency text");
}

static void test_text(void) {
    printf("test_text\n");
    check_text(433920000, "433.92");
    check_text(315000000, "315.00");
    check_text(868350000, "868.35");
    // Not whole 10 kHz steps: the third decimal is kept, not cut off.
    check_text(433075000, "433.075");
    check_text(303875000, "303.875");
    check_text(434775000, "434.775");
    check_text(433001000, "433.001");
    // Below 1 kHz is dropped.
    check_text(433920500, "433.92");
    check_text(433075999, "433.075");
    check_text(RG_FREQ_MAX_HZ, "962.00");
    check_text(UINT32_MAX, "4294.967");

    char small[4] = "xyz";
    rg_freq_text(433920000, small, sizeof(small));
    CHECK(small[3] == '\0' && strlen(small) == 3, "truncated to the buffer");
    rg_freq_text(433920000, NULL, 8);
    rg_freq_text(433920000, small, 0);
    CHECK(strcmp(small, "433") == 0, "zero-size buffer untouched");
}

static void test_range(void) {
    printf("test_range\n");
    CHECK(rg_freq_in_range(433920000), "433.92 MHz");
    CHECK(rg_freq_in_range(RG_FREQ_MIN_HZ), "lower bound included");
    CHECK(rg_freq_in_range(RG_FREQ_MAX_HZ), "upper bound included");
    CHECK(!rg_freq_in_range(RG_FREQ_MIN_HZ - 1), "below the range");
    CHECK(!rg_freq_in_range(RG_FREQ_MAX_HZ + 1), "above the range");
    CHECK(!rg_freq_in_range(0), "zero");
    CHECK(!rg_freq_in_range(2400000000u), "2.4 GHz");
    CHECK(RG_FREQ_MIN_KHZ == 281000 && RG_FREQ_MAX_KHZ == 962000, "kHz bounds");
    CHECK(RG_FREQ_MAX_KHZ <= 0x7FFFFFFF, "kHz bounds fit the number keyboard's int32");
}

static void test_nearest(void) {
    printf("test_nearest\n");
    static const uint32_t list[] = {315000000, 390000000, 433920000, 868350000};
    const size_t n = sizeof(list) / sizeof(list[0]);
    CHECK(rg_freq_nearest(list, n, 433920000) == 2, "exact match");
    CHECK(rg_freq_nearest(list, n, 433500000) == 2, "closest above");
    CHECK(rg_freq_nearest(list, n, 400000000) == 1, "closest below");
    CHECK(rg_freq_nearest(list, n, 100000000) == 0, "below every entry");
    CHECK(rg_freq_nearest(list, n, 960000000) == 3, "above every entry");
    CHECK(rg_freq_nearest(list, n, 352500000) == 0, "tie goes to the first");
    CHECK(rg_freq_nearest(list, 0, 433920000) == 0, "empty list");

    CHECK(rg_freq_find(list, n, 390000000) == 1, "found");
    CHECK(rg_freq_find(list, n, 390000001) == n, "not found");
    CHECK(rg_freq_find(list, 0, 390000000) == 0, "empty list: not found");
}

int main(void) {
    test_text();
    test_range();
    test_nearest();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) return 1;
    printf("ALL FREQUENCY TESTS PASSED\n");
    return 0;
}
