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
#define RADIOGEDDON_SETTINGS_TEMP EXT_PATH("apps_data/radiogeddon/settings.tmp")

typedef struct {
    uint32_t frequency; // Hz, receiver / default frequency
    uint8_t preset_index; // index into radiogeddon_presets
    uint32_t scan_mask; // bit i enables radiogeddon_frequencies[i] in the scanner
    uint16_t scan_dwell_ms; // listening time per frequency
    uint8_t scan_threshold_db; // activity threshold above the noise floor
    bool scan_hold_on_hit; // stop sweeping on the frequency that became active
    uint32_t hop_mask; // bit i enables radiogeddon_frequencies[i] in the hopper
    uint16_t hop_dwell_ms; // time on a quiet frequency before hopping on
    uint16_t hop_hold_ms; // time to stay after activity was last seen
    bool hop_auto_record; // record RAW while holding on activity, save automatically
    uint8_t db_sort; // Database sort order (RgDbSort), kept from the last visit
    bool radio_external; // use an external CC1101 module when one answers
    bool ext_power; // switch on 5 V (GPIO pin 1) for the external module
    uint32_t radio_heap; // heap the last measured receive session took (0: none)
    uint32_t radio_heap_fw; // firmware it was measured on (rg_mem_firmware_tag)
    // Full edition. Both editions read and write these, so a settings file
    // keeps them when the other edition saves it.
    uint32_t range_start_hz; // range scanner: first frequency
    uint32_t range_end_hz; // range scanner: last frequency
    uint32_t range_step_hz; // range scanner: grid step
    uint16_t range_dwell_ms; // range scanner: listening time per point
    bool range_hold_on_hit; // range scanner: pause on the point that became active
    uint8_t scan_source; // RadioGeddonSource: Scanner frequencies
    uint8_t hop_source; // RadioGeddonSource: Hopper frequencies
    uint32_t freq_step_hz; // Settings frequency step (0: step through the list)
} RadioGeddonSettings;

/** Where the Scanner and the Hopper take their frequencies from. */
typedef enum {
    RadioGeddonSourceList, // the built-in list, filtered by the scan/hop mask
    RadioGeddonSourceFavorites, // the favorites file (Full edition)
    RadioGeddonSourceCount,
} RadioGeddonSource;

#define RADIOGEDDON_RANGE_DEFAULT_START 433000000UL
#define RADIOGEDDON_RANGE_DEFAULT_END   435000000UL
#define RADIOGEDDON_RANGE_DEFAULT_STEP  25000UL

/** Default hopper list: the frequencies in radiogeddon_hopper_frequencies. */
uint32_t radiogeddon_settings_default_hop_mask(void);

/** Fill @p settings with defaults. */
void radiogeddon_settings_default(RadioGeddonSettings* settings);

/** Load from the SD card. Missing or invalid fields keep their defaults. */
void radiogeddon_settings_load(Storage* storage, RadioGeddonSettings* settings);

/** Save to the SD card, replacing the file only once the new one is complete.
 * Returns false on I/O failure (the previous file is then kept). */
bool radiogeddon_settings_save(Storage* storage, const RadioGeddonSettings* settings);
