#include "radiogeddon_settings.h"
#include "radiogeddon_subghz.h"

#include <lib/flipper_format/flipper_format.h>

#define SETTINGS_FILE_TYPE    "RadioGeddon Settings"
#define SETTINGS_FILE_VERSION 1

static uint32_t radiogeddon_settings_all_mask(void) {
    size_t n = radiogeddon_frequencies_count;
    if(n >= 32) return 0xFFFFFFFFu;
    return (1u << n) - 1u;
}

void radiogeddon_settings_default(RadioGeddonSettings* settings) {
    settings->frequency = RADIOGEDDON_FREQUENCY_DEFAULT;
    settings->preset_index = 1; // AM 650
    settings->scan_mask = radiogeddon_settings_all_mask();
    settings->scan_dwell_ms = 10;
    settings->scan_threshold_db = 10;
    settings->scan_hold_on_hit = false;
}

static bool radiogeddon_settings_known_frequency(uint32_t hz) {
    for(size_t i = 0; i < radiogeddon_frequencies_count; i++) {
        if(radiogeddon_frequencies[i] == hz) return true;
    }
    return false;
}

void radiogeddon_settings_load(Storage* storage, RadioGeddonSettings* settings) {
    radiogeddon_settings_default(settings);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    uint32_t version = 0;
    uint32_t v = 0;
    bool b = false;

    do {
        if(!flipper_format_file_open_existing(ff, RADIOGEDDON_SETTINGS_PATH)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;
        if(!furi_string_equal_str(type, SETTINGS_FILE_TYPE) || version != SETTINGS_FILE_VERSION)
            break;

        // Each field is optional and validated on its own; read in file order
        // with a rewind before each so missing keys don't skip later ones.
        if(flipper_format_read_uint32(ff, "Frequency", &v, 1) &&
           radiogeddon_settings_known_frequency(v))
            settings->frequency = v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Preset", &v, 1) && v < radiogeddon_presets_count)
            settings->preset_index = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Scan_mask", &v, 1)) {
            v &= radiogeddon_settings_all_mask();
            if(v) settings->scan_mask = v;
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Scan_dwell_ms", &v, 1) && v >= 1 && v <= 1000)
            settings->scan_dwell_ms = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Scan_threshold_db", &v, 1) && v >= 1 && v <= 60)
            settings->scan_threshold_db = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Scan_hold_on_hit", &b, 1)) settings->scan_hold_on_hit = b;
    } while(false);

    furi_string_free(type);
    flipper_format_free(ff);
}

bool radiogeddon_settings_save(Storage* storage, const RadioGeddonSettings* settings) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, RADIOGEDDON_SETTINGS_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILE_TYPE, SETTINGS_FILE_VERSION)) break;
        uint32_t v = settings->frequency;
        if(!flipper_format_write_uint32(ff, "Frequency", &v, 1)) break;
        v = settings->preset_index;
        if(!flipper_format_write_uint32(ff, "Preset", &v, 1)) break;
        v = settings->scan_mask;
        if(!flipper_format_write_uint32(ff, "Scan_mask", &v, 1)) break;
        v = settings->scan_dwell_ms;
        if(!flipper_format_write_uint32(ff, "Scan_dwell_ms", &v, 1)) break;
        v = settings->scan_threshold_db;
        if(!flipper_format_write_uint32(ff, "Scan_threshold_db", &v, 1)) break;
        bool b = settings->scan_hold_on_hit;
        if(!flipper_format_write_bool(ff, "Scan_hold_on_hit", &b, 1)) break;
        ok = true;
    } while(false);
    flipper_format_free(ff);
    return ok;
}
