/**
 * Host-side unit tests for the transmit check (helpers/rg_txpolicy.c), in both
 * the Catalog (region gate on) and Full (off) configurations.
 * Run: make -C test
 */
#include "../helpers/rg_txpolicy.h"
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

static RgTxFacts facts(bool device, bool hardware, bool provisioned, bool allows) {
    RgTxFacts f = {device, hardware, provisioned, allows};
    return f;
}

static void test_catalog(void) {
    printf("test_catalog\n");
    CHECK(rg_txpolicy_check(&(RgTxFacts){true, true, true, true}, true) == RgTxAllowed, "all yes");
    // Every single missing fact refuses, with its own reason.
    RgTxFacts f = facts(false, true, true, true);
    CHECK(rg_txpolicy_check(&f, true) == RgTxDeniedDevice, "device rejects");
    f = facts(true, false, true, true);
    CHECK(rg_txpolicy_check(&f, true) == RgTxDeniedHardware, "hardware rejects");
    f = facts(true, true, false, true);
    CHECK(rg_txpolicy_check(&f, true) == RgTxDeniedNoRegion, "no region: refused");
    f = facts(true, true, true, false);
    CHECK(rg_txpolicy_check(&f, true) == RgTxDeniedRegion, "region forbids");
    // An unprovisioned firmware reports "allowed" for nothing it knows: the
    // unknown region wins even if the table lookup said yes.
    f = facts(true, true, false, false);
    CHECK(rg_txpolicy_check(&f, true) == RgTxDeniedNoRegion, "no region before region table");
    // All 16 combinations: allowed only when every fact holds.
    for(unsigned m = 0; m < 16; m++) {
        f = facts(m & 1, m & 2, m & 4, m & 8);
        bool allowed = rg_txpolicy_check(&f, true) == RgTxAllowed;
        CHECK(allowed == (m == 15), "catalog: allowed only with every fact");
    }
}

static void test_full(void) {
    printf("test_full\n");
    // Without the region gate only the radio's own tuning check applies; the
    // firmware's transmit rules then decide at subghz_devices_set_tx().
    for(unsigned m = 0; m < 16; m++) {
        RgTxFacts f = facts(m & 1, m & 2, m & 4, m & 8);
        RgTxVerdict v = rg_txpolicy_check(&f, false);
        CHECK(v == ((m & 1) ? RgTxAllowed : RgTxDeniedDevice), "full: device check only");
    }
}

static void test_text(void) {
    printf("test_text\n");
    char out[96];
    rg_txpolicy_explain(RgTxDeniedRegion, "EU", "315.00", out, sizeof(out));
    CHECK(strcmp(out, "Region EU does not\nallow TX on\n315.00 MHz.") == 0, "region text");
    rg_txpolicy_explain(RgTxDeniedRegion, NULL, "315.00", out, sizeof(out));
    CHECK(strstr(out, "Region -- ") != NULL, "missing region name");
    rg_txpolicy_explain(RgTxDeniedNoRegion, "", NULL, out, sizeof(out));
    CHECK(strstr(out, "no\nregion info") != NULL, "no region text");
    rg_txpolicy_explain(RgTxDeniedDevice, "US", "300.00", out, sizeof(out));
    CHECK(strstr(out, "cannot tune\n300.00 MHz") != NULL, "device text");
    rg_txpolicy_explain(RgTxDeniedHardware, "US", "290.00", out, sizeof(out));
    CHECK(strstr(out, "290.00 MHz is outside") != NULL, "hardware text");
    char small[8];
    rg_txpolicy_explain(RgTxDeniedRegion, "EU", "315.00", small, sizeof(small));
    CHECK(strlen(small) == 7, "truncated to the buffer");
    rg_txpolicy_explain(RgTxDeniedRegion, "EU", "315.00", NULL, 10);
    rg_txpolicy_explain(RgTxDeniedRegion, "EU", "315.00", small, 0);
    CHECK(strlen(rg_txpolicy_short(RgTxAllowed)) == 0, "allowed: no reason");
    CHECK(strcmp(rg_txpolicy_short(RgTxDeniedRegion), "Region forbids") == 0, "short reason");
    // Every text is plain ASCII (the Flipper font has no other glyphs).
    for(int v = RgTxAllowed; v <= RgTxDeniedRegion; v++) {
        rg_txpolicy_explain((RgTxVerdict)v, "EU", "433.92", out, sizeof(out));
        for(const char* p = out; *p; p++) {
            CHECK((unsigned char)*p < 0x80, "ascii explanation");
        }
    }
}

int main(void) {
    test_catalog();
    test_full();
    test_text();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
