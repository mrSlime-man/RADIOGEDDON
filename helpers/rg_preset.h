/**
 * @file rg_preset.h
 * @brief Checks a CC1101 custom preset before it is loaded into the radio.
 *
 * A .sub file recorded with a custom preset stores it as
 * `Custom_preset_data: XX YY XX YY .. 00 00 ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ`: register
 * and value pairs, a 00 register that ends them, then 8 PA table bytes. The
 * firmware loads that array without bounds: it writes pairs until it meets a
 * 00 register and then copies the 8 bytes after it. A damaged file would make
 * it read past the buffer, and an address above the configuration registers
 * is a command strobe (0x35 starts transmitting) rather than a setting.
 *
 * Pure C with no SDK includes, unit-tested on the host (test/test_preset.c).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Highest CC1101 configuration register (TEST0). Above it are the command
 * strobes (0x30-0x3D), the PA table (0x3E) and the FIFO (0x3F). */
#define RG_PRESET_LAST_CONFIG_REG 0x2E
/* PA table bytes after the end of the register list. */
#define RG_PRESET_PA_TABLE_SIZE   8

typedef enum {
    RgPresetOk = 0,
    RgPresetEmpty, /* no data */
    RgPresetNoEnd, /* no 00 register ends the list */
    RgPresetShortPaTable, /* fewer than 8 PA table bytes after the end */
    RgPresetBadRegister, /* an address that is not a configuration register */
} RgPresetResult;

/**
 * Check @p size bytes of custom preset data the way the firmware will read
 * them. On RgPresetBadRegister, @p bad (if not NULL) receives the address.
 */
RgPresetResult rg_preset_check(const uint8_t* data, size_t size, uint8_t* bad);

/** Short reason for a result, for messages and reports. */
const char* rg_preset_result_text(RgPresetResult result);
