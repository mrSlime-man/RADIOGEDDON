#pragma once

/**
 * RadioGeddon product edition, chosen at build time.
 *
 * - Catalog (RADIOGEDDON_EDITION_CATALOG): the Official firmware / Flipper
 *   Apps Catalog edition. application.fam defines it, so the catalog's plain
 *   `ufbt` build produces it with no extra flags.
 * - Full (RADIOGEDDON_EDITION_FULL): the flagship edition for RogueMaster,
 *   Momentum and Unleashed. scripts/stage_edition.py writes a copy of the
 *   source whose application.fam defines it instead.
 *
 * A build that defines neither is a Catalog build: an unknown build gets the
 * more restricted behaviour, never the other way round.
 *
 * Every edition difference is a RG_FEATURE_* switch below, so the rest of the
 * code asks "is this feature in?" rather than "which edition is this?". Pure
 * C with no SDK includes; host-tested in both configurations
 * (test/test_edition.c).
 */

#if defined(RADIOGEDDON_EDITION_FULL) && defined(RADIOGEDDON_EDITION_CATALOG)
#error "Define only one of RADIOGEDDON_EDITION_FULL and RADIOGEDDON_EDITION_CATALOG"
#endif

#if defined(RADIOGEDDON_EDITION_FULL)
#define RG_EDITION_FULL 1
#else
#define RG_EDITION_FULL 0
#endif

/* Shown on the About screen and in exported reports. */
#if RG_EDITION_FULL
#define RG_EDITION_NAME "Full"
#else
#define RG_EDITION_NAME "Catalog"
#endif

/* Range scanner: start/end/step sweeps across every band the radio tunes,
 * scan profiles and favorite-frequency scan sources. */
#define RG_FEATURE_RANGE_SCAN RG_EDITION_FULL

/* Favorite / custom frequency list, usable by Receive, Scanner and Hopper. */
#define RG_FEATURE_FAVORITES RG_EDITION_FULL

/* Fine frequency stepping in Settings (1 kHz to 1 MHz steps across gaps). */
#define RG_FEATURE_FREQ_STEP RG_EDITION_FULL

/* Detected receive bands of the radio in use (Settings > Radio bands). */
#define RG_FEATURE_BAND_INFO RG_EDITION_FULL

/* Checksum / CRC structure hypotheses in Unknown Protocol Analysis: tests
 * whether the last bits of the distinct frames of a capture behave like a
 * common checksum of the bits before them. */
#define RG_FEATURE_CHECKSUM_HINTS RG_EDITION_FULL

/* Before any transmission, require the firmware to have a provisioned region
 * that allows the frequency (furi_hal_region_*) and the internal radio's
 * hardware check (furi_hal_subghz_is_frequency_valid), and explain a refusal.
 * The Full edition leaves this to the firmware's own transmit authorization
 * (subghz_devices_set_tx), which both editions always go through. */
#define RG_FEATURE_REGION_TX_GATE (!RG_EDITION_FULL)
