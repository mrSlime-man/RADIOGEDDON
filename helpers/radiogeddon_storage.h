/**
 * @file radiogeddon_storage.h
 * @brief On-SD-card signal database layout and .sub parsing helpers.
 *
 * Recordings and analysis live under /ext/apps_data/radiogeddon so they are
 * grouped and never collide with the firmware's own /ext/subghz library,
 * while remaining fully compatible with the standard .sub file format.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#define RADIOGEDDON_APP_FOLDER     EXT_PATH("apps_data/radiogeddon")
#define RADIOGEDDON_SIGNALS_FOLDER RADIOGEDDON_APP_FOLDER "/signals"
#define RADIOGEDDON_SCANS_FOLDER   RADIOGEDDON_APP_FOLDER "/scans"
#define RADIOGEDDON_SUB_EXTENSION  ".sub"

// .sub file identification (firmware-compatible).
#define RADIOGEDDON_SUB_FILE_TYPE     "Flipper SubGhz Key File"
#define RADIOGEDDON_SUB_FILE_VERSION  1
#define RADIOGEDDON_RAW_FILE_TYPE_STR "Flipper SubGhz RAW File"

/** Classification of a loaded .sub file. */
typedef enum {
    RadioGeddonSignalKindUnknown,
    RadioGeddonSignalKindRaw,
    RadioGeddonSignalKindProtocol,
} RadioGeddonSignalKind;

/** Parsed, in-memory representation of a .sub file for display/analysis. */
typedef struct {
    bool valid;
    RadioGeddonSignalKind kind;
    FuriString* name; // display name (file stem)
    FuriString* protocol; // "RAW" or protocol name
    FuriString* preset; // modulation preset name
    uint32_t frequency; // Hz
    uint32_t bit_count; // protocol: number of bits (0 if unknown)
    uint64_t key; // protocol: key value (0 if none)
    size_t raw_sample_count; // RAW: number of timing samples
    uint32_t raw_min_us; // RAW: shortest |duration| seen
    uint32_t raw_max_us; // RAW: longest |duration| seen
} RadioGeddonLoadedSignal;

/** Ensure the application's SD-card folders exist. */
void radiogeddon_storage_ensure_paths(Storage* storage);

/** Build a timestamped default file name (no path), e.g. "RG_20261009_163500". */
void radiogeddon_storage_default_name(FuriString* out);

/** Build a full path under the signals folder for a bare @p name (adds .sub). */
void radiogeddon_storage_make_path(FuriString* out, const char* name);

/** Write a serialized .sub payload (already including header) to @p path. */
bool radiogeddon_storage_write_serialized(
    Storage* storage,
    const char* path,
    FuriString* serialized);

/** Parse a .sub file into @p out. Returns false on open/parse failure. */
bool radiogeddon_storage_load(Storage* storage, const char* path, RadioGeddonLoadedSignal* out);

/**
 * Load the RAW timing samples (signed microsecond durations) from a RAW .sub
 * file into @p buf, up to @p cap values. Returns the number of samples read
 * (0 if the file is not RAW or has no samples). Used by the analysis engine.
 */
size_t radiogeddon_storage_load_raw_samples(
    Storage* storage,
    const char* path,
    int32_t* buf,
    size_t cap);

/** Initialize an empty loaded-signal struct (allocates strings). */
void radiogeddon_loaded_signal_init(RadioGeddonLoadedSignal* sig);

/** Release a loaded-signal struct (frees strings). */
void radiogeddon_loaded_signal_reset(RadioGeddonLoadedSignal* sig);

/**
 * Build a signals-folder path for @p name that does not overwrite an existing
 * file: "<name>.sub", else "<name>_2.sub" ... "<name>_99.sub". Returns false
 * if all are taken.
 */
bool radiogeddon_storage_make_unique_path(Storage* storage, FuriString* out, const char* name);

/** Build a timestamped scan-results path, e.g. ".../scans/SCAN_20261009_163500.csv". */
void radiogeddon_storage_make_scan_path(FuriString* out);
