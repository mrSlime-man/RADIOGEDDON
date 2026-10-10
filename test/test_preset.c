/**
 * Host-side unit tests for the custom preset check (helpers/rg_preset.c).
 * All presets here are written for the test; the firmware's own presets are
 * checked by test_fwpreset (make -C test decoders).
 * Run: make -C test
 */
#include "../helpers/rg_preset.h"
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

#define N(a) (sizeof(a) / sizeof((a)[0]))

/* Shaped like a stock-app AM preset: pairs, 00 00, 8 PA table bytes. */
static const uint8_t good[] = {0x02, 0x0D, 0x03, 0x07, 0x08, 0x32, 0x0B, 0x06, 0x14, 0x00, 0x13,
                               0x00, 0x12, 0x30, 0x11, 0x32, 0x10, 0x17, 0x18, 0x18, 0x19, 0x18,
                               0x1D, 0x91, 0x1C, 0x00, 0x1B, 0x07, 0x20, 0xFB, 0x22, 0x11, 0x21,
                               0xB6, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void test_valid(void) {
    printf("test_valid\n");
    CHECK(rg_preset_check(good, N(good), NULL) == RgPresetOk, "stock-shaped preset");

    // Only the end marker and the PA table: nothing is written, still loadable.
    static const uint8_t only_pa[] = {0x00, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(rg_preset_check(only_pa, N(only_pa), NULL) == RgPresetOk, "empty register list");

    // The firmware ignores the byte after the 00 register and anything after
    // the PA table; so does the check.
    static const uint8_t odd_end[] = {0x02, 0x0D, 0x00, 0x55, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK(rg_preset_check(odd_end, N(odd_end), NULL) == RgPresetOk, "trailing bytes ignored");

    // TEST0 (0x2E) is the highest configuration register and is allowed.
    static const uint8_t test0[] = {0x2E, 0x09, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(rg_preset_check(test0, N(test0), NULL) == RgPresetOk, "TEST0 allowed");
}

static void test_damaged(void) {
    printf("test_damaged\n");
    CHECK(rg_preset_check(NULL, 10, NULL) == RgPresetEmpty, "no buffer");
    CHECK(rg_preset_check(good, 0, NULL) == RgPresetEmpty, "no bytes");

    // Cut before the end marker: the firmware would keep reading past it.
    CHECK(rg_preset_check(good, 34, NULL) == RgPresetNoEnd, "cut before 00 00");
    CHECK(rg_preset_check(good, 33, NULL) == RgPresetNoEnd, "cut mid-pair");
    static const uint8_t one[] = {0x02};
    CHECK(rg_preset_check(one, N(one), NULL) == RgPresetNoEnd, "a register with no value");

    // End marker present but the PA table is short, by every amount.
    int short_ok = 1;
    for(size_t cut = 35; cut < N(good); cut++) {
        if(rg_preset_check(good, cut, NULL) != RgPresetShortPaTable) short_ok = 0;
    }
    CHECK(short_ok, "every cut inside the PA table is refused");
    static const uint8_t end_only[] = {0x00};
    CHECK(rg_preset_check(end_only, N(end_only), NULL) == RgPresetShortPaTable, "00 alone");
}

static void test_bad_registers(void) {
    printf("test_bad_registers\n");
    // 0x35 is the STX strobe: written as a "register" it would start
    // transmitting before the firmware's region check runs.
    static const uint8_t stx[] = {0x02, 0x0D, 0x35, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t bad = 0;
    CHECK(rg_preset_check(stx, N(stx), &bad) == RgPresetBadRegister && bad == 0x35, "STX strobe");

    int all_refused = 1;
    for(unsigned reg = RG_PRESET_LAST_CONFIG_REG + 1; reg <= 0xFF; reg++) {
        uint8_t p[] = {(uint8_t)reg, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
        bad = 0;
        if(rg_preset_check(p, N(p), &bad) != RgPresetBadRegister || bad != reg) all_refused = 0;
    }
    CHECK(all_refused, "every address above TEST0 refused (strobes, PA table, FIFO, burst)");

    int all_allowed = 1;
    for(unsigned reg = 1; reg <= RG_PRESET_LAST_CONFIG_REG; reg++) {
        uint8_t p[] = {(uint8_t)reg, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
        if(rg_preset_check(p, N(p), NULL) != RgPresetOk) all_allowed = 0;
    }
    CHECK(all_allowed, "every configuration register 0x01-0x2E allowed");

    // A value byte equal to a strobe address is a value, not an address.
    static const uint8_t value[] = {0x02, 0x35, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(rg_preset_check(value, N(value), NULL) == RgPresetOk, "strobe-valued data is fine");

    // A bad address after the end marker is PA table data, not a register.
    static const uint8_t after[] = {0x00, 0x00, 0x35, 0x36, 0, 0, 0, 0, 0, 0};
    CHECK(rg_preset_check(after, N(after), NULL) == RgPresetOk, "PA bytes are not registers");

    CHECK(rg_preset_check(stx, N(stx), NULL) == RgPresetBadRegister, "NULL for bad is fine");
}

static void test_text(void) {
    printf("test_text\n");
    CHECK(strcmp(rg_preset_result_text(RgPresetOk), "OK") == 0, "ok text");
    const RgPresetResult all[] = {
        RgPresetEmpty, RgPresetNoEnd, RgPresetShortPaTable, RgPresetBadRegister};
    int distinct = 1;
    for(size_t i = 0; i < N(all); i++) {
        const char* t = rg_preset_result_text(all[i]);
        if(!t || !*t || strlen(t) > 24) distinct = 0;
        for(size_t j = 0; j < i; j++)
            if(strcmp(t, rg_preset_result_text(all[j])) == 0) distinct = 0;
    }
    CHECK(distinct, "each reason has its own short text (fits a popup line)");
    CHECK(*rg_preset_result_text((RgPresetResult)99) != '\0', "unknown value still has text");
}

int main(void) {
    test_valid();
    test_damaged();
    test_bad_registers();
    test_text();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) return 1;
    printf("ALL PRESET TESTS PASSED\n");
    return 0;
}
