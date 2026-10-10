#include "radiogeddon_profiles.h"
#include "../radiogeddon_edition.h"

#if RG_EDITION_FULL

#include "radiogeddon_bands.h"
#include "rg_db.h"
#include "rg_freq.h"
#include "rg_range.h"

#include <lib/flipper_format/flipper_format.h>
#include <string.h>

#define FAVORITES_FILE_TYPE "RadioGeddon Favorites"
#define PROFILE_FILE_TYPE   "RadioGeddon Scan Profile"
#define LIST_FILE_VERSION   1

/* ---- Favorites ----------------------------------------------------------- */

RadioGeddonFavoriteResult radiogeddon_favorites_add(RadioGeddonFavorites* favorites, uint32_t hz) {
    if(!rg_freq_in_range(hz)) return RadioGeddonFavoriteInvalid;
    size_t pos = 0;
    while(pos < favorites->count && favorites->freq[pos] < hz)
        pos++;
    if(pos < favorites->count && favorites->freq[pos] == hz) return RadioGeddonFavoriteExists;
    if(favorites->count >= RADIOGEDDON_FAVORITES_MAX) return RadioGeddonFavoriteFull;
    memmove(
        &favorites->freq[pos + 1],
        &favorites->freq[pos],
        (favorites->count - pos) * sizeof(favorites->freq[0]));
    favorites->freq[pos] = hz;
    favorites->count++;
    return RadioGeddonFavoriteAdded;
}

bool radiogeddon_favorites_remove(RadioGeddonFavorites* favorites, size_t index) {
    if(index >= favorites->count) return false;
    memmove(
        &favorites->freq[index],
        &favorites->freq[index + 1],
        (favorites->count - index - 1) * sizeof(favorites->freq[0]));
    favorites->count--;
    return true;
}

void radiogeddon_favorites_load(Storage* storage, RadioGeddonFavorites* favorites) {
    favorites->count = 0;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    uint32_t version = 0;
    uint32_t count = 0;
    do {
        if(!flipper_format_file_open_existing(ff, RADIOGEDDON_FAVORITES_PATH)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;
        if(!furi_string_equal_str(type, FAVORITES_FILE_TYPE) || version != LIST_FILE_VERSION)
            break;
        if(!flipper_format_get_value_count(ff, "Frequency", &count) || count == 0) break;
        // Read at most twice the limit: duplicates and bad entries are
        // dropped below, anything more is not ours.
        if(count > RADIOGEDDON_FAVORITES_MAX * 2u) count = RADIOGEDDON_FAVORITES_MAX * 2u;
        uint32_t values[RADIOGEDDON_FAVORITES_MAX * 2u];
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Frequency", values, (uint16_t)count)) break;
        for(uint32_t i = 0; i < count; i++) {
            radiogeddon_favorites_add(favorites, values[i]);
        }
    } while(false);
    furi_string_free(type);
    flipper_format_free(ff);
}

static bool radiogeddon_profiles_replace(Storage* storage, const char* temp, const char* path) {
    FS_Error err = storage_common_rename(storage, temp, path);
    if(err == FSE_EXIST) {
        // Firmware that does not replace on rename.
        storage_common_remove(storage, path);
        err = storage_common_rename(storage, temp, path);
    }
    return err == FSE_OK;
}

bool radiogeddon_favorites_save(Storage* storage, const RadioGeddonFavorites* favorites) {
    radiogeddon_storage_ensure_paths(storage);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, RADIOGEDDON_FAVORITES_TEMP)) break;
        if(!flipper_format_write_header_cstr(ff, FAVORITES_FILE_TYPE, LIST_FILE_VERSION)) break;
        if(!flipper_format_write_comment_cstr(ff, "Frequencies in Hz")) break;
        // An empty list is written without the key and reads back empty.
        if(favorites->count > 0 &&
           !flipper_format_write_uint32(
               ff, "Frequency", favorites->freq, (uint16_t)favorites->count))
            break;
        ok = true;
    } while(false);
    ok = flipper_format_file_close(ff) && ok;
    flipper_format_free(ff);
    if(ok)
        ok = radiogeddon_profiles_replace(
            storage, RADIOGEDDON_FAVORITES_TEMP, RADIOGEDDON_FAVORITES_PATH);
    if(!ok) storage_common_remove(storage, RADIOGEDDON_FAVORITES_TEMP);
    return ok;
}

/* ---- Scan profiles ------------------------------------------------------- */

bool radiogeddon_profile_valid(const RadioGeddonScanProfile* p) {
    return rg_freq_in_range(p->start_hz) && rg_freq_in_range(p->end_hz) &&
           p->start_hz <= p->end_hz && p->step_hz >= RG_RANGE_MIN_STEP_HZ &&
           p->step_hz <= RG_RANGE_MAX_STEP_HZ && p->dwell_ms >= 1 && p->dwell_ms <= 1000 &&
           p->threshold_db >= 1 && p->threshold_db <= 60 &&
           p->preset_index < radiogeddon_presets_count;
}

void radiogeddon_profile_path(FuriString* out, const char* name) {
    furi_string_printf(out, "%s/%s%s", RADIOGEDDON_PROFILES_FOLDER, name, RADIOGEDDON_PROFILE_EXT);
}

bool radiogeddon_profile_exists(Storage* storage, const char* name) {
    FuriString* path = furi_string_alloc();
    radiogeddon_profile_path(path, name);
    bool exists = storage_common_exists(storage, furi_string_get_cstr(path));
    furi_string_free(path);
    return exists;
}

bool radiogeddon_profile_save(
    Storage* storage,
    const char* name,
    const RadioGeddonScanProfile* profile,
    bool overwrite) {
    if(rg_db_check_name(name) != RgDbNameOk || strlen(name) >= RADIOGEDDON_PROFILE_NAME_LEN)
        return false;
    if(!radiogeddon_profile_valid(profile)) return false;
    radiogeddon_storage_ensure_paths(storage);
    storage_common_mkdir(storage, RADIOGEDDON_PROFILES_FOLDER);

    FuriString* path = furi_string_alloc();
    FuriString* temp = furi_string_alloc();
    radiogeddon_profile_path(path, name);
    furi_string_printf(temp, "%s/.profile.tmp", RADIOGEDDON_PROFILES_FOLDER);
    bool ok = false;
    if(overwrite || !storage_common_exists(storage, furi_string_get_cstr(path))) {
        FlipperFormat* ff = flipper_format_file_alloc(storage);
        do {
            if(!flipper_format_file_open_always(ff, furi_string_get_cstr(temp))) break;
            if(!flipper_format_write_header_cstr(ff, PROFILE_FILE_TYPE, LIST_FILE_VERSION)) break;
            uint32_t v = profile->start_hz;
            if(!flipper_format_write_uint32(ff, "Start", &v, 1)) break;
            v = profile->end_hz;
            if(!flipper_format_write_uint32(ff, "End", &v, 1)) break;
            v = profile->step_hz;
            if(!flipper_format_write_uint32(ff, "Step", &v, 1)) break;
            v = profile->dwell_ms;
            if(!flipper_format_write_uint32(ff, "Dwell_ms", &v, 1)) break;
            v = profile->threshold_db;
            if(!flipper_format_write_uint32(ff, "Threshold_db", &v, 1)) break;
            bool b = profile->hold_on_hit;
            if(!flipper_format_write_bool(ff, "Hold_on_hit", &b, 1)) break;
            if(!flipper_format_write_string_cstr(
                   ff, "Preset", radiogeddon_presets[profile->preset_index].file_name))
                break;
            ok = true;
        } while(false);
        ok = flipper_format_file_close(ff) && ok;
        flipper_format_free(ff);
        if(ok) {
            ok = radiogeddon_profiles_replace(
                storage, furi_string_get_cstr(temp), furi_string_get_cstr(path));
        }
        if(!ok) storage_common_remove(storage, furi_string_get_cstr(temp));
    }
    furi_string_free(temp);
    furi_string_free(path);
    return ok;
}

bool radiogeddon_profile_load(Storage* storage, const char* name, RadioGeddonScanProfile* profile) {
    FuriString* path = furi_string_alloc();
    FuriString* text = furi_string_alloc();
    radiogeddon_profile_path(path, name);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    RadioGeddonScanProfile p;
    memset(&p, 0, sizeof(p));
    uint32_t version = 0;
    uint32_t v = 0;
    bool ok = false;
    do {
        if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(path))) break;
        if(!flipper_format_read_header(ff, text, &version)) break;
        if(!furi_string_equal_str(text, PROFILE_FILE_TYPE) || version != LIST_FILE_VERSION) break;
        if(!flipper_format_read_uint32(ff, "Start", &p.start_hz, 1)) break;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "End", &p.end_hz, 1)) break;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Step", &p.step_hz, 1)) break;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Dwell_ms", &v, 1) || v > 1000) break;
        p.dwell_ms = (uint16_t)v;
        flipper_format_rewind(ff);
        if(!flipper_format_read_uint32(ff, "Threshold_db", &v, 1) || v > 60) break;
        p.threshold_db = (uint8_t)v;
        flipper_format_rewind(ff);
        if(!flipper_format_read_bool(ff, "Hold_on_hit", &p.hold_on_hit, 1)) break;
        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Preset", text)) break;
        int32_t preset = radiogeddon_preset_find_file_name(furi_string_get_cstr(text));
        if(preset < 0) break;
        p.preset_index = (uint8_t)preset;
        ok = radiogeddon_profile_valid(&p);
    } while(false);
    if(ok) *profile = p;
    flipper_format_free(ff);
    furi_string_free(text);
    furi_string_free(path);
    return ok;
}

bool radiogeddon_profile_delete(Storage* storage, const char* name) {
    if(rg_db_check_name(name) != RgDbNameOk) return false;
    FuriString* path = furi_string_alloc();
    radiogeddon_profile_path(path, name);
    bool ok = storage_common_remove(storage, furi_string_get_cstr(path)) == FSE_OK;
    furi_string_free(path);
    return ok;
}

size_t radiogeddon_profile_list(
    Storage* storage,
    char (*names)[RADIOGEDDON_PROFILE_NAME_LEN],
    size_t max) {
    size_t n = 0;
    File* dir = storage_file_alloc(storage);
    char name[RADIOGEDDON_PROFILE_NAME_LEN + 8];
    FileInfo info;
    if(storage_dir_open(dir, RADIOGEDDON_PROFILES_FOLDER)) {
        while(n < max && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) || name[0] == '.') continue;
            size_t len = strlen(name);
            size_t ext = strlen(RADIOGEDDON_PROFILE_EXT);
            if(len <= ext || strcmp(name + len - ext, RADIOGEDDON_PROFILE_EXT) != 0) continue;
            name[len - ext] = '\0';
            if(len - ext >= RADIOGEDDON_PROFILE_NAME_LEN) continue;
            // Insertion into the sorted list.
            size_t pos = n;
            while(pos > 0 && strcmp(names[pos - 1], name) > 0) {
                memcpy(names[pos], names[pos - 1], RADIOGEDDON_PROFILE_NAME_LEN);
                pos--;
            }
            memcpy(names[pos], name, len - ext + 1);
            n++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return n;
}

#endif /* RG_EDITION_FULL */
