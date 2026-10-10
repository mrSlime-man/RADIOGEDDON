/**
 * @file radiogeddon_settings.h
 * @brief User settings persisted to the SD card across launches.
 *
 * Stored as a small Flipper Format file. A missing, unreadable or partially
 * valid file falls back to defaults field by field, so a corrupted settings
 * file never prevents the app from starting.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#define RADIOGEDDON_SETTINGS_PATH EXT_PATH("apps_data/radiogeddon/settings.txt")

typedef struct {
    uint32_t frequency; // Hz, receiver / default frequency
    uint8_t preset_index; // index into radiogeddon_presets
    uint32_t scan_mask; // bit i enables radiogeddon_frequencies[i] in the scanner
    uint16_t scan_dwell_ms; // listening time per frequency
    uint8_t scan_threshold_db; // activity threshold above the noise floor
    bool scan_hold_on_hit; // stop sweeping on the frequency that became active
} RadioGeddonSettings;

/** Fill @p settings with defaults. */
void radiogeddon_settings_default(RadioGeddonSettings* settings);

/** Load from the SD card. Missing or invalid fields keep their defaults. */
void radiogeddon_settings_load(Storage* storage, RadioGeddonSettings* settings);

/** Save to the SD card. Returns false on I/O failure. */
bool radiogeddon_settings_save(Storage* storage, const RadioGeddonSettings* settings);
