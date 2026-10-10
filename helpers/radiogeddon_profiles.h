/**
 * @file radiogeddon_profiles.h
 * @brief Favorite frequencies and saved range-scan profiles on the SD card.
 *
 * Both are small Flipper Format files under /ext/apps_data/radiogeddon, the
 * folder both editions share:
 * - favorites.txt holds up to RADIOGEDDON_FAVORITES_MAX frequencies, kept
 *   sorted and without duplicates;
 * - profiles/<name>.txt holds one range-scan setup each.
 * Every value is validated on load; a damaged favorites file loses only its
 * bad entries, a damaged profile is refused as a whole. Saves write a
 * temporary file and rename it over the old one only when complete.
 *
 * Used by the Full edition's Favorites and Range Scanner screens; the code is
 * shared so the Catalog edition never damages these files.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

#include "radiogeddon_storage.h"

#define RADIOGEDDON_FAVORITES_PATH  RADIOGEDDON_APP_FOLDER "/favorites.txt"
#define RADIOGEDDON_FAVORITES_TEMP  RADIOGEDDON_APP_FOLDER "/favorites.tmp"
#define RADIOGEDDON_PROFILES_FOLDER RADIOGEDDON_APP_FOLDER "/profiles"
#define RADIOGEDDON_PROFILE_EXT     ".txt"

/* As many as the Scanner and Hopper take (RADIOGEDDON_SCANNER_MAX_CHANNELS,
 * RG_HOP_MAX_CHANNELS), so the whole list can be scanned or hopped. */
#define RADIOGEDDON_FAVORITES_MAX 24u

#define RADIOGEDDON_PROFILE_NAME_LEN 32u
#define RADIOGEDDON_PROFILES_MAX     32u

typedef struct {
    uint32_t freq[RADIOGEDDON_FAVORITES_MAX];
    size_t count;
} RadioGeddonFavorites;

typedef enum {
    RadioGeddonFavoriteAdded,
    RadioGeddonFavoriteExists,
    RadioGeddonFavoriteFull,
    RadioGeddonFavoriteInvalid, // outside every firmware's tuning range
} RadioGeddonFavoriteResult;

/** Read the favorites; a missing file is an empty list. */
void radiogeddon_favorites_load(Storage* storage, RadioGeddonFavorites* favorites);

/** Write the favorites. Returns false on a card error (the old file is kept). */
bool radiogeddon_favorites_save(Storage* storage, const RadioGeddonFavorites* favorites);

/** Insert @p hz in order. Does not save. */
RadioGeddonFavoriteResult radiogeddon_favorites_add(RadioGeddonFavorites* favorites, uint32_t hz);

/** Remove entry @p index. Does not save. Returns false if out of range. */
bool radiogeddon_favorites_remove(RadioGeddonFavorites* favorites, size_t index);

typedef struct {
    uint32_t start_hz;
    uint32_t end_hz;
    uint32_t step_hz;
    uint16_t dwell_ms;
    uint8_t threshold_db;
    bool hold_on_hit;
    uint8_t preset_index;
} RadioGeddonScanProfile;

/** True if every field of @p profile is in range (start <= end and so on). */
bool radiogeddon_profile_valid(const RadioGeddonScanProfile* profile);

/** Full path of profile @p name. */
void radiogeddon_profile_path(FuriString* out, const char* name);

/**
 * Save @p profile as @p name (checked with rg_db_check_name). An existing
 * profile of that name is replaced only with @p overwrite; otherwise the call
 * returns false and leaves it alone.
 */
bool radiogeddon_profile_save(
    Storage* storage,
    const char* name,
    const RadioGeddonScanProfile* profile,
    bool overwrite);

/** Load profile @p name. False if missing, unreadable or any field is invalid. */
bool radiogeddon_profile_load(Storage* storage, const char* name, RadioGeddonScanProfile* profile);

/** Delete profile @p name. Returns false if it could not be removed. */
bool radiogeddon_profile_delete(Storage* storage, const char* name);

/** True if profile @p name exists. */
bool radiogeddon_profile_exists(Storage* storage, const char* name);

/**
 * List profile names (without extension), sorted, at most @p max. Returns how
 * many were written to @p names.
 */
size_t radiogeddon_profile_list(
    Storage* storage,
    char (*names)[RADIOGEDDON_PROFILE_NAME_LEN],
    size_t max);
