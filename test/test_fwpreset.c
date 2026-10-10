/**
 * The custom preset check (helpers/rg_preset.c) on the firmware's own presets:
 * the register arrays its Sub-GHz driver loads for every built-in modulation
 * (lib/subghz/devices/cc1101_configs.c) and the custom presets in the example
 * settings file it ships (setting_user.example). Every one must pass, ending
 * where the firmware ends it. Fetched like the format tests (GPL, from the
 * pinned Official release, never committed).
 * Run: make -C test decoders
 */
#include "../helpers/rg_preset.h"

// The firmware's preset arrays, compiled in so their sizes are known here.
#include "lib/subghz/devices/cc1101_configs.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FW_DIR
#define FW_DIR "build/fw"
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

#define PRESET(a) {#a, a, sizeof(a)}

static const struct {
    const char* name;
    const uint8_t* data;
    size_t size;
} builtin[] = {
    PRESET(subghz_device_cc1101_preset_ook_270khz_async_regs),
    PRESET(subghz_device_cc1101_preset_ook_650khz_async_regs),
    PRESET(subghz_device_cc1101_preset_2fsk_dev2_38khz_async_regs),
    PRESET(subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs),
    PRESET(subghz_device_cc1101_preset_msk_99_97kb_async_regs),
    PRESET(subghz_device_cc1101_preset_gfsk_9_99kb_async_regs),
};

/* Where the firmware's loader stops: the 00 register. */
static size_t end_of(const uint8_t* d, size_t n) {
    size_t i = 0;
    while(i < n && d[i])
        i += 2;
    return i;
}

static void test_builtin(void) {
    printf("test_builtin\n");
    for(size_t k = 0; k < sizeof(builtin) / sizeof(builtin[0]); k++) {
        uint8_t bad = 0;
        RgPresetResult r = rg_preset_check(builtin[k].data, builtin[k].size, &bad);
        size_t end = end_of(builtin[k].data, builtin[k].size);
        printf(
            "  %-55s %3zu bytes, %2zu registers: %s\n",
            builtin[k].name,
            builtin[k].size,
            end / 2,
            rg_preset_result_text(r));
        CHECK(r == RgPresetOk, "built-in preset passes");
        // Exactly the end marker and the PA table follow the registers.
        CHECK(builtin[k].size == end + 2 + RG_PRESET_PA_TABLE_SIZE, "ends with 00 00 + PA table");
        CHECK(end >= 2, "writes at least one register");
    }
}

static int hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void test_example_settings(void) {
    printf("test_example_settings\n");
    const char* path = FW_DIR
        "/applications/main/subghz/resources/subghz/assets/setting_user.example";
    FILE* f = fopen(path, "r");
    CHECK(f != NULL, "example settings file present");
    if(!f) return;
    char line[512];
    int presets = 0;
    const char* key = "#Custom_preset_data: ";
    while(fgets(line, sizeof(line), f)) {
        // The examples are commented out; the format comment line has no hex.
        if(strncmp(line, key, strlen(key)) != 0) continue;
        uint8_t data[128];
        size_t n = 0;
        const char* p = line + strlen(key);
        int ok = 1;
        while(*p && *p != '\n' && *p != '\r') {
            if(*p == ' ') {
                p++;
                continue;
            }
            int hi = hexval(p[0]);
            int lo = p[1] ? hexval(p[1]) : -1;
            if(hi < 0 || lo < 0 || n >= sizeof(data)) {
                ok = 0;
                break;
            }
            data[n++] = (uint8_t)(hi * 16 + lo);
            p += 2;
        }
        CHECK(ok && n > 0, "example preset parses as hex");
        if(!ok) continue;
        RgPresetResult r = rg_preset_check(data, n, NULL);
        printf("  example preset %d: %zu bytes: %s\n", presets + 1, n, rg_preset_result_text(r));
        CHECK(r == RgPresetOk, "example preset passes");
        CHECK(n == end_of(data, n) + 2 + RG_PRESET_PA_TABLE_SIZE, "example ends with 00 00 + PA");
        presets++;
    }
    fclose(f);
    CHECK(presets == 2, "both example presets found (AM_1, AM_2)");
}

int main(void) {
    test_builtin();
    test_example_settings();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) return 1;
    printf("ALL FIRMWARE PRESET TESTS PASSED\n");
    return 0;
}
