#pragma once

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Helpers for working with saved Sub-GHz recordings (.sub files in
 * /ext/subghz). All functions are defensive: they open their own storage
 * handle, validate the file, and never leave a handle dangling on error.
 */

#define RG_SUBGHZ_FOLDER    EXT_PATH("subghz")
#define RG_SUBGHZ_EXTENSION ".sub"

/** Maximum RAW samples loaded per file when comparing (bounds memory). */
#define RG_COMPARE_MAX_SAMPLES 4096

/**
 * Build a human-readable summary of a .sub file (type, frequency, preset,
 * protocol, sample/sample-ish count) into `out`.
 * @return true if the file was a readable Flipper .sub file.
 */
bool rg_storage_read_info(const char* path, FuriString* out);

/** Delete a file. @return true on success. */
bool rg_storage_delete(const char* path);

/** @return true if a file exists at path. */
bool rg_storage_exists(const char* path);

/**
 * Compare two RAW recordings and produce a similarity report in `out`.
 * The score (0-100) combines frequency/preset agreement with a tolerant
 * sample-by-sample match of the RAW timing data.
 * @param out_score optional out: 0-100 similarity.
 * @return true if both files were readable RAW recordings.
 */
bool rg_storage_compare(const char* path_a, const char* path_b, FuriString* out, int* out_score);

#ifdef __cplusplus
}
#endif
