/**
 * @file rg_txpolicy.h
 * @brief The app's own check before a transmission, from facts the firmware gives.
 *
 * Every transmission also goes through the firmware's radio driver
 * (subghz_devices_set_tx), which applies the firmware's own transmit rules in
 * both editions; this check runs before the radio is touched.
 *
 * - Catalog edition (region gate on): the radio in use must accept the
 *   frequency, the internal radio's hardware check
 *   (furi_hal_subghz_is_frequency_valid) must accept it, the firmware must
 *   know its region (furi_hal_region_is_provisioned) and that region must
 *   allow the frequency (furi_hal_region_is_frequency_allowed). Anything
 *   unknown refuses. There is no way to choose another region in the app.
 * - Full edition (region gate off): only the radio in use must accept the
 *   frequency; regional rules are the installed firmware's to apply.
 *
 * Pure C with no SDK includes; host-tested (test/test_txpolicy.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool device_valid; // the radio in use accepts the frequency (subghz_devices_is_frequency_valid)
    bool hardware_valid; // furi_hal_subghz_is_frequency_valid
    bool region_provisioned; // furi_hal_region_is_provisioned
    bool region_allows; // furi_hal_region_is_frequency_allowed
} RgTxFacts;

typedef enum {
    RgTxAllowed,
    RgTxDeniedDevice, // the radio in use cannot tune the frequency
    RgTxDeniedHardware, // outside the radio hardware's transmit bands
    RgTxDeniedNoRegion, // the firmware has no region information
    RgTxDeniedRegion, // the firmware's region does not allow the frequency
} RgTxVerdict;

/** Decide from @p facts; @p region_gate is RG_FEATURE_REGION_TX_GATE. */
RgTxVerdict rg_txpolicy_check(const RgTxFacts* facts, bool region_gate);

/** One-line reason for a refusal ("Region forbids"); "" for RgTxAllowed. */
const char* rg_txpolicy_short(RgTxVerdict verdict);

/**
 * The on-screen explanation, e.g. "Region EU does not\nallow TX on\n433.92 MHz."
 * @p region is the firmware's region name (may be NULL), @p freq_text the
 * frequency as shown elsewhere. Always NUL-terminated (truncated if needed).
 */
void rg_txpolicy_explain(
    RgTxVerdict verdict,
    const char* region,
    const char* freq_text,
    char* out,
    size_t out_size);
