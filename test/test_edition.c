/**
 * The edition switches (radiogeddon_edition.h), compiled three times by the
 * Makefile: as the Full edition, as the Catalog edition, and with no edition
 * defined (which must be a Catalog build). EXPECT_FULL says which result each
 * build must give. A build defining both editions must not compile; the
 * Makefile checks that too.
 * Run: make -C test
 */
#include "../radiogeddon_edition.h"
#include <stdio.h>
#include <string.h>

#ifndef EXPECT_FULL
#error "EXPECT_FULL must be 0 or 1"
#endif

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

int main(void) {
    printf("test_edition (%s build)\n", EXPECT_FULL ? "Full" : "Catalog");
    CHECK(RG_EDITION_FULL == EXPECT_FULL, "edition");
    CHECK(strcmp(RG_EDITION_NAME, EXPECT_FULL ? "Full" : "Catalog") == 0, "edition name");
    // Full-only research features.
    CHECK(RG_FEATURE_RANGE_SCAN == EXPECT_FULL, "range scanner only in Full");
    CHECK(RG_FEATURE_FAVORITES == EXPECT_FULL, "favorites only in Full");
    CHECK(RG_FEATURE_FREQ_STEP == EXPECT_FULL, "fine stepping only in Full");
    CHECK(RG_FEATURE_BAND_INFO == EXPECT_FULL, "band info only in Full");
    CHECK(RG_FEATURE_CHECKSUM_HINTS == EXPECT_FULL, "checksum hints only in Full");
    CHECK(RG_FEATURE_WATERFALL == EXPECT_FULL, "waterfall only in Full");
    CHECK(RG_FEATURE_BITSTREAM == EXPECT_FULL, "bitstream explorer only in Full");
    CHECK(RG_FEATURE_MULTI_COMPARE == EXPECT_FULL, "multi-capture compare only in Full");
    CHECK(RG_FEATURE_SESSIONS == EXPECT_FULL, "sessions only in Full");
    // The app's own region gate is in every build except Full.
    CHECK(RG_FEATURE_REGION_TX_GATE == !EXPECT_FULL, "region gate in Catalog");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
