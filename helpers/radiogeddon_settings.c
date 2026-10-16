#include "radiogeddon_settings.h"
#include "radiogeddon_safefile.h"
#include "radiogeddon_bands.h"
#include "rg_db.h"
#include "rg_freq.h"
#include "rg_range.h"
#include "rg_ui.h"
#include "rg_mascot.h"
#include "rg_adapt.h"
#include "rg_journal.h"

#include <lib/flipper_format/flipper_format.h>
#include <stddef.h>

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
    settings->hop_mask = radiogeddon_settings_default_hop_mask();
    settings->hop_dwell_ms = 200;
    settings->hop_hold_ms = 2000;
    settings->hop_auto_record = false;
    settings->db_sort = RgDbSortDate;
    settings->radio_external = false;
    settings->ext_power = true; // as the firmware's Sub-GHz app does
    settings->radio_heap = 0; // not measured yet
    settings->radio_heap_fw = 0;
    settings->range_start_hz = RADIOGEDDON_RANGE_DEFAULT_START;
    settings->range_end_hz = RADIOGEDDON_RANGE_DEFAULT_END;
    settings->range_step_hz = RADIOGEDDON_RANGE_DEFAULT_STEP;
    settings->range_dwell_ms = 5;
    settings->range_hold_on_hit = false;
    settings->scan_source = RadioGeddonSourceList;
    settings->hop_source = RadioGeddonSourceList;
    settings->freq_step_hz = 0;
    // A settings file from before beta 8 has none of these: the Full edition
    // starts in Dolphin Lab. The Catalog edition has only the Classic menus
    // and ignores the style, but keeps the same default, so whichever
    // edition writes the file first leaves the Full edition's default.
    settings->ui_style = RgUiStyleDolphinLab;
    settings->ui_feedback = RgUiFeedbackOff;
    settings->ui_transitions = true;
    // Beta 9: a settings file from before has none of these.
    settings->ui_animation = RgAnimNormal;
    settings->assistant = true;
    settings->research_xp = true;
    RgMascotLook look;
    rg_mascot_look_default(&look);
    memcpy(settings->look, &look, sizeof(settings->look));
    settings->decoration = RgDecoNone;
    settings->scan_mode = RgAdaptStandard;
    settings->log_interval_s = 60;
    settings->log_minutes = 60;
    settings->log_max_kb = 256;
}

uint32_t radiogeddon_settings_default_hop_mask(void) {
    uint32_t mask = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        for(size_t j = 0; j < radiogeddon_hopper_frequencies_count; j++) {
            if(radiogeddon_frequencies[i] == radiogeddon_hopper_frequencies[j]) mask |= 1u << i;
        }
    }
    return mask ? mask : radiogeddon_settings_all_mask();
}

/* How a field is stored and checked. */
typedef enum {
    FieldU8,
    FieldU16,
    FieldU32,
    FieldBool,
    FieldSpecial, // read and written by name below (checked together, or not a plain range)
} FieldKind;

typedef struct {
    const char* key;
    uint16_t offset;
    uint8_t kind; // FieldKind
    uint32_t min;
    uint32_t max;
} SettingsField;

#define F(key, member, kind, min, max) \
    {key, (uint16_t)offsetof(RadioGeddonSettings, member), kind, min, max}

/* Every key, in the file's order. Each is optional and checked on its own;
 * a value out of range keeps the default. */
static const SettingsField settings_fields[] = {
    F("Frequency", frequency, FieldSpecial, 0, 0),
    F("Preset", preset_index, FieldSpecial, 0, 0),
    F("Scan_mask", scan_mask, FieldSpecial, 0, 0),
    F("Scan_dwell_ms", scan_dwell_ms, FieldU16, 1, 1000),
    F("Scan_threshold_db", scan_threshold_db, FieldU8, 1, 60),
    F("Scan_hold_on_hit", scan_hold_on_hit, FieldBool, 0, 1),
    F("Hop_mask", hop_mask, FieldSpecial, 0, 0),
    F("Hop_dwell_ms", hop_dwell_ms, FieldU16, 50, 10000),
    F("Hop_hold_ms", hop_hold_ms, FieldU16, 100, 60000),
    F("Hop_auto_record", hop_auto_record, FieldBool, 0, 1),
    F("Db_sort", db_sort, FieldU8, 0, RgDbSortCount - 1),
    F("Radio_external", radio_external, FieldBool, 0, 1),
    F("Ext_5V", ext_power, FieldBool, 0, 1),
    F("Radio_heap", radio_heap, FieldSpecial, 0, 0),
    F("Radio_heap_fw", radio_heap_fw, FieldSpecial, 0, 0),
    F("Range_start", range_start_hz, FieldSpecial, 0, 0),
    F("Range_end", range_end_hz, FieldSpecial, 0, 0),
    F("Range_step", range_step_hz, FieldU32, RG_RANGE_MIN_STEP_HZ, RG_RANGE_MAX_STEP_HZ),
    F("Range_dwell_ms", range_dwell_ms, FieldU16, 1, 1000),
    F("Range_hold_on_hit", range_hold_on_hit, FieldBool, 0, 1),
    F("Scan_source", scan_source, FieldU8, 0, RadioGeddonSourceCount - 1),
    F("Hop_source", hop_source, FieldU8, 0, RadioGeddonSourceCount - 1),
    F("Freq_step", freq_step_hz, FieldU32, 0, 1000000),
    F("Ui_style", ui_style, FieldU8, 0, RgUiStyleCount - 1),
    F("Ui_feedback", ui_feedback, FieldU8, 0, RgUiFeedbackCount - 1),
    F("Ui_transitions", ui_transitions, FieldBool, 0, 1),
    F("Ui_animation", ui_animation, FieldU8, 0, RgAnimCount - 1),
    F("Assistant", assistant, FieldBool, 0, 1),
    F("Dolphin_look", look, FieldSpecial, 0, 0),
    F("Lab_decoration", decoration, FieldU8, 0, RgDecoCount - 1),
    F("Scan_mode", scan_mode, FieldU8, 0, RgAdaptModeCount - 1),
    F("Log_interval_s", log_interval_s, FieldU16, 10, 3600),
    F("Log_minutes", log_minutes, FieldU16, 0, 1440),
    F("Log_max_kb", log_max_kb, FieldU16, 16, 4096),
    F("Research_xp", research_xp, FieldBool, 0, 1),
};

#undef F

static void settings_set(RadioGeddonSettings* s, const SettingsField* f, uint32_t v) {
    uint8_t* p = (uint8_t*)s + f->offset;
    switch(f->kind) {
    case FieldU8:
        *p = (uint8_t)v;
        break;
    case FieldU16: {
        uint16_t x = (uint16_t)v;
        memcpy(p, &x, sizeof(x));
        break;
    }
    case FieldU32:
        memcpy(p, &v, sizeof(v));
        break;
    case FieldBool:
        *(bool*)p = v != 0;
        break;
    default:
        break;
    }
}

static uint32_t settings_get(const RadioGeddonSettings* s, const SettingsField* f) {
    const uint8_t* p = (const uint8_t*)s + f->offset;
    switch(f->kind) {
    case FieldU8:
        return *p;
    case FieldU16: {
        uint16_t x;
        memcpy(&x, p, sizeof(x));
        return x;
    }
    case FieldU32: {
        uint32_t x;
        memcpy(&x, p, sizeof(x));
        return x;
    }
    case FieldBool:
        return *(const bool*)p ? 1u : 0u;
    default:
        return 0;
    }
}

/* Read @p count values of @p key from the start of the file. */
static bool settings_read(FlipperFormat* ff, const char* key, uint32_t* v, uint16_t count) {
    flipper_format_rewind(ff);
    return flipper_format_read_uint32(ff, key, v, count);
}

/* The fields checked together, or against tables, read by name. */
static void settings_load_special(FlipperFormat* ff, RadioGeddonSettings* settings) {
    uint32_t v = 0, w = 0;
    // Any frequency some firmware can tune, so a custom one survives a
    // restart; the radio in use is asked again at startup.
    if(settings_read(ff, "Frequency", &v, 1) && rg_freq_in_range(v)) settings->frequency = v;
    if(settings_read(ff, "Preset", &v, 1) && v < radiogeddon_presets_count)
        settings->preset_index = (uint8_t)v;
    if(settings_read(ff, "Scan_mask", &v, 1)) {
        v &= radiogeddon_settings_all_mask();
        if(v) settings->scan_mask = v;
    }
    if(settings_read(ff, "Hop_mask", &v, 1)) {
        v &= radiogeddon_settings_all_mask();
        if(v) settings->hop_mask = v;
    }
    // A measured session cost is only used together with its firmware tag.
    if(settings_read(ff, "Radio_heap", &v, 1) && v <= 256u * 1024u &&
       settings_read(ff, "Radio_heap_fw", &w, 1) && w != 0) {
        settings->radio_heap = v;
        settings->radio_heap_fw = w;
    }
    // Range scanner: start and end are kept together, so a file can never
    // leave a start above the end.
    if(settings_read(ff, "Range_start", &v, 1) && rg_freq_in_range(v) &&
       settings_read(ff, "Range_end", &w, 1) && rg_freq_in_range(w) && w >= v) {
        settings->range_start_hz = v;
        settings->range_end_hz = w;
    }
    uint32_t look[6];
    if(settings_read(ff, "Dolphin_look", look, 6)) {
        RgMascotLook l;
        uint8_t* b = (uint8_t*)&l;
        for(size_t i = 0; i < 6; i++)
            b[i] = (uint8_t)(look[i] < 256 ? look[i] : 255);
        rg_mascot_look_sanitize(&l);
        memcpy(settings->look, &l, sizeof(settings->look));
    }
}

static const RgSafeFile settings_file = {
    RADIOGEDDON_SETTINGS_PATH,
    RADIOGEDDON_SETTINGS_TEMP,
    RADIOGEDDON_SETTINGS_MARK,
};

void radiogeddon_settings_load(Storage* storage, RadioGeddonSettings* settings) {
    radiogeddon_settings_default(settings);
    // A save cut off by a reboot is finished (or dropped) before reading.
    radiogeddon_safefile_recover(storage, &settings_file);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    uint32_t version = 0;

    do {
        if(!flipper_format_file_open_existing(ff, RADIOGEDDON_SETTINGS_PATH)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;
        if(!furi_string_equal_str(type, SETTINGS_FILE_TYPE) || version != SETTINGS_FILE_VERSION)
            break;
        // Each field is optional and validated on its own; every read
        // starts from the top, so missing keys don't skip later ones.
        for(size_t i = 0; i < COUNT_OF(settings_fields); i++) {
            const SettingsField* f = &settings_fields[i];
            if(f->kind == FieldSpecial) continue;
            uint32_t v = 0;
            bool got;
            if(f->kind == FieldBool) {
                bool b = false;
                flipper_format_rewind(ff);
                got = flipper_format_read_bool(ff, f->key, &b, 1);
                v = b;
            } else {
                got = settings_read(ff, f->key, &v, 1);
            }
            if(got && v >= f->min && v <= f->max) settings_set(settings, f, v);
        }
        settings_load_special(ff, settings);
    } while(false);

    furi_string_free(type);
    flipper_format_free(ff);
}

bool radiogeddon_settings_save(Storage* storage, const RadioGeddonSettings* settings) {
    // Write a new file next to the old one and swap it in only when complete
    // (radiogeddon_safefile.h), so neither a failed write nor a reboot during
    // the swap leaves a cut-off settings file.
    if(!radiogeddon_safefile_begin(storage, &settings_file)) return false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, RADIOGEDDON_SETTINGS_TEMP)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILE_TYPE, SETTINGS_FILE_VERSION)) break;
        bool written = true;
        for(size_t i = 0; i < COUNT_OF(settings_fields) && written; i++) {
            const SettingsField* f = &settings_fields[i];
            const uint8_t* p = (const uint8_t*)settings + f->offset;
            uint32_t v;
            if(f->kind == FieldBool) {
                bool b = *(const bool*)p;
                written = flipper_format_write_bool(ff, f->key, &b, 1);
            } else if(f->kind != FieldSpecial) {
                v = settings_get(settings, f);
                written = flipper_format_write_uint32(ff, f->key, &v, 1);
            } else if(f->offset == offsetof(RadioGeddonSettings, look)) {
                uint32_t look[6];
                for(size_t k = 0; k < 6; k++)
                    look[k] = settings->look[k];
                written = flipper_format_write_uint32(ff, f->key, look, 6);
            } else if(f->offset == offsetof(RadioGeddonSettings, preset_index)) {
                v = settings->preset_index;
                written = flipper_format_write_uint32(ff, f->key, &v, 1);
            } else {
                // The remaining specials are all 32-bit.
                memcpy(&v, p, sizeof(v));
                written = flipper_format_write_uint32(ff, f->key, &v, 1);
            }
        }
        ok = written;
    } while(false);
    ok = flipper_format_file_close(ff) && ok;
    flipper_format_free(ff);
    if(ok) return radiogeddon_safefile_commit(storage, &settings_file);
    storage_common_remove(storage, RADIOGEDDON_SETTINGS_TEMP);
    return false;
}
