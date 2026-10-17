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

/*
 * Code sections. The firmware's ELF loader gives every allocatable section a
 * heap block of its own (lib/flipper_application/elf/elf_file.c), and a
 * launch fails when the largest free block is smaller than a section plus
 * 1 KB. After another app (or this one) ran, the heap is in pieces, so the
 * largest block a launch asks for decides whether the app starts again.
 *
 * The app itself (RG_SECTIONED, set in application.fam) is cut into code
 * sections of 6-16 KB, each a group of whole source files, so a call inside
 * a file stays a direct branch:
 *
 *   .text    radiogeddon.c and the helpers not listed below
 *   .rgtext  the screens' code (scenes/, RG_UI_TEXT)
 *   .rgview  the custom views (views/, RG_VIEW_TEXT)
 *   .rgcore  the radio, scanner, recorder and decoder helpers (RG_CORE_TEXT)
 *   .rglab   the Dolphin Lab, journal and assistant helpers (RG_LAB_TEXT)
 *   .rgtool  the analysis engines, resident only in the Catalog edition
 *            (RG_TOOL_TEXT; the Full edition has them in modules)
 *
 * A named section keeps all of a file's functions (the linker drops unused
 * code per section), so only files the app uses whole are grouped
 * (scripts/text_sections.py lists them and checks every function).
 * Section names must not start with ".text" (the SDK's linker script merges
 * those). A module (RG_SECTIONED not set) keeps beta 12's two sections: the
 * screens and views in .rgtext, the rest in .text. Only on the device (ARM);
 * host builds keep the default section.
 */
#if defined(__arm__) && !defined(RG_NO_UI_SECTION)
#define RG_UI_TEXT __attribute__((section(".rgtext")))
#if defined(RG_SECTIONED)
#define RG_VIEW_TEXT __attribute__((section(".rgview")))
#define RG_CORE_TEXT __attribute__((section(".rgcore")))
#define RG_LAB_TEXT  __attribute__((section(".rglab")))
#define RG_TOOL_TEXT __attribute__((section(".rgtool")))
#else
#define RG_VIEW_TEXT RG_UI_TEXT
#define RG_CORE_TEXT
#define RG_LAB_TEXT
#define RG_TOOL_TEXT
#endif
#else
#define RG_UI_TEXT
#define RG_VIEW_TEXT
#define RG_CORE_TEXT
#define RG_LAB_TEXT
#define RG_TOOL_TEXT
#endif

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

/* Waterfall: the history of a range scan's sequential RSSI sweeps drawn as
 * frequency across, time down (helpers/rg_waterfall.h). Needs the range
 * scanner's engine. */
#define RG_FEATURE_WATERFALL (RG_EDITION_FULL && RG_FEATURE_RANGE_SCAN)

/* Bitstream Explorer: binary / hex / frame / difference / field views of the
 * bits the analyzer infers from a RAW capture (helpers/rg_bits.h). */
#define RG_FEATURE_BITSTREAM RG_EDITION_FULL

/* Multi-Capture Compare: several saved captures analysed one after another
 * and compared field by field (helpers/rg_multi.h). */
#define RG_FEATURE_MULTI_COMPARE RG_EDITION_FULL

/* Research Sessions: named groups of recordings kept in small text files on
 * the SD card (helpers/rg_session.h). */
#define RG_FEATURE_SESSIONS RG_EDITION_FULL

/* Dolphin Lab: the animated dolphin mascot, the lab dashboard home screen,
 * the Dolphin Lab / Cyber Lab / Classic UI styles, cards, transitions,
 * optional UI feedback and the Dolphin Lab room (helpers/rg_mascot.h,
 * helpers/radiogeddon_ui.h). The Catalog edition keeps the Classic menus. */
#define RG_FEATURE_DOLPHIN_LAB RG_EDITION_FULL

/* Signal Fingerprint: compact structural summaries of saved recordings, an
 * index of them on the SD card and Find Similar Signals (helpers/rg_fp.h). */
#define RG_FEATURE_FINGERPRINT RG_EDITION_FULL

/* Pulse Studio: waveform, frames, bits and timing of a RAW capture on one
 * screen, with the inferred pulse-to-bit mapping (a module). */
#define RG_FEATURE_PULSE_STUDIO RG_EDITION_FULL

/* Adaptive Scanner: a Scanner whose schedule favours frequencies with
 * observed activity, with a bounded revisit interval (helpers/rg_adapt.h). */
#define RG_FEATURE_ADAPTIVE_SCAN RG_EDITION_FULL

/* RF Activity Logger: long passive observation of a frequency list, logged
 * to the SD card with coverage (helpers/rg_actlog.h). Uses the Adaptive
 * Scanner's schedule. */
#define RG_FEATURE_ACTIVITY_LOG (RG_EDITION_FULL && RG_FEATURE_ADAPTIVE_SCAN)

/* Research Journal, Research XP and achievements: counts of real research
 * events kept in one small file (helpers/rg_journal.h). */
#define RG_FEATURE_JOURNAL RG_EDITION_FULL

/* Dolphin Assistant: rule-based next-step suggestions from what the app
 * really did (helpers/rg_assist.h). Needs the journal's event stream. */
#define RG_FEATURE_ASSISTANT (RG_EDITION_FULL && RG_FEATURE_JOURNAL)
