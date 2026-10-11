#include "radiogeddon_settings.h"
#include "radiogeddon_bands.h"
#include "rg_db.h"
#include "rg_freq.h"
#include "rg_range.h"
#include "rg_ui.h"
#include "rg_mascot.h"
#include "rg_adapt.h"
#include "rg_journal.h"

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
        // Any frequency some firmware can tune, so a custom one survives a
        // restart; the radio in use is asked again at startup.
        if(flipper_format_read_uint32(ff, "Frequency", &v, 1) && rg_freq_in_range(v))
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
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Hop_mask", &v, 1)) {
            v &= radiogeddon_settings_all_mask();
            if(v) settings->hop_mask = v;
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Hop_dwell_ms", &v, 1) && v >= 50 && v <= 10000)
            settings->hop_dwell_ms = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Hop_hold_ms", &v, 1) && v >= 100 && v <= 60000)
            settings->hop_hold_ms = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Hop_auto_record", &b, 1)) settings->hop_auto_record = b;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Db_sort", &v, 1) && v < RgDbSortCount)
            settings->db_sort = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Radio_external", &b, 1)) settings->radio_external = b;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Ext_5V", &b, 1)) settings->ext_power = b;
        // A measured session cost is only used together with its firmware tag.
        uint32_t fw = 0;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Radio_heap", &v, 1) && v <= 256u * 1024u) {
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Radio_heap_fw", &fw, 1) && fw != 0) {
                settings->radio_heap = v;
                settings->radio_heap_fw = fw;
            }
        }
        // Range scanner: start and end are kept together, so a file can never
        // leave a start above the end.
        uint32_t end = 0;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Range_start", &v, 1) && rg_freq_in_range(v)) {
            flipper_format_rewind(ff);
            if(flipper_format_read_uint32(ff, "Range_end", &end, 1) && rg_freq_in_range(end) &&
               end >= v) {
                settings->range_start_hz = v;
                settings->range_end_hz = end;
            }
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Range_step", &v, 1) && v >= RG_RANGE_MIN_STEP_HZ &&
           v <= RG_RANGE_MAX_STEP_HZ)
            settings->range_step_hz = v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Range_dwell_ms", &v, 1) && v >= 1 && v <= 1000)
            settings->range_dwell_ms = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Range_hold_on_hit", &b, 1))
            settings->range_hold_on_hit = b;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Scan_source", &v, 1) && v < RadioGeddonSourceCount)
            settings->scan_source = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Hop_source", &v, 1) && v < RadioGeddonSourceCount)
            settings->hop_source = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Freq_step", &v, 1) && v <= 1000000u)
            settings->freq_step_hz = v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Ui_style", &v, 1) && v < RgUiStyleCount)
            settings->ui_style = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Ui_feedback", &v, 1) && v < RgUiFeedbackCount)
            settings->ui_feedback = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Ui_transitions", &b, 1)) settings->ui_transitions = b;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Ui_animation", &v, 1) && v < RgAnimCount)
            settings->ui_animation = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_bool(ff, "Assistant", &b, 1)) settings->assistant = b;
        flipper_format_rewind(ff);
        uint32_t look[6];
        if(flipper_format_read_uint32(ff, "Dolphin_look", look, 6)) {
            RgMascotLook l = {
                .phones = (uint8_t)(look[0] > 1 ? 1 : look[0]),
                .goggles = (uint8_t)(look[1] < 256 ? look[1] : 255),
                .antenna = (uint8_t)(look[2] < 256 ? look[2] : 255),
                .tool = (uint8_t)(look[3] < 256 ? look[3] : 255),
                .expression = (uint8_t)(look[4] < 256 ? look[4] : 255),
                .accessory = (uint8_t)(look[5] < 256 ? look[5] : 255),
            };
            rg_mascot_look_sanitize(&l);
            memcpy(settings->look, &l, sizeof(settings->look));
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Lab_decoration", &v, 1) && v < RgDecoCount)
            settings->decoration = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Scan_mode", &v, 1) && v < RgAdaptModeCount)
            settings->scan_mode = (uint8_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Log_interval_s", &v, 1) && v >= 10 && v <= 3600)
            settings->log_interval_s = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Log_minutes", &v, 1) && v <= 1440)
            settings->log_minutes = (uint16_t)v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Log_max_kb", &v, 1) && v >= 16 && v <= 4096)
            settings->log_max_kb = (uint16_t)v;
    } while(false);

    furi_string_free(type);
    flipper_format_free(ff);
}

bool radiogeddon_settings_save(Storage* storage, const RadioGeddonSettings* settings) {
    // Write a new file next to the old one and swap it in only when complete,
    // so a failed write never leaves a cut-off settings file.
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, RADIOGEDDON_SETTINGS_TEMP)) break;
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
        v = settings->hop_mask;
        if(!flipper_format_write_uint32(ff, "Hop_mask", &v, 1)) break;
        v = settings->hop_dwell_ms;
        if(!flipper_format_write_uint32(ff, "Hop_dwell_ms", &v, 1)) break;
        v = settings->hop_hold_ms;
        if(!flipper_format_write_uint32(ff, "Hop_hold_ms", &v, 1)) break;
        b = settings->hop_auto_record;
        if(!flipper_format_write_bool(ff, "Hop_auto_record", &b, 1)) break;
        v = settings->db_sort;
        if(!flipper_format_write_uint32(ff, "Db_sort", &v, 1)) break;
        b = settings->radio_external;
        if(!flipper_format_write_bool(ff, "Radio_external", &b, 1)) break;
        b = settings->ext_power;
        if(!flipper_format_write_bool(ff, "Ext_5V", &b, 1)) break;
        v = settings->radio_heap;
        if(!flipper_format_write_uint32(ff, "Radio_heap", &v, 1)) break;
        v = settings->radio_heap_fw;
        if(!flipper_format_write_uint32(ff, "Radio_heap_fw", &v, 1)) break;
        v = settings->range_start_hz;
        if(!flipper_format_write_uint32(ff, "Range_start", &v, 1)) break;
        v = settings->range_end_hz;
        if(!flipper_format_write_uint32(ff, "Range_end", &v, 1)) break;
        v = settings->range_step_hz;
        if(!flipper_format_write_uint32(ff, "Range_step", &v, 1)) break;
        v = settings->range_dwell_ms;
        if(!flipper_format_write_uint32(ff, "Range_dwell_ms", &v, 1)) break;
        b = settings->range_hold_on_hit;
        if(!flipper_format_write_bool(ff, "Range_hold_on_hit", &b, 1)) break;
        v = settings->scan_source;
        if(!flipper_format_write_uint32(ff, "Scan_source", &v, 1)) break;
        v = settings->hop_source;
        if(!flipper_format_write_uint32(ff, "Hop_source", &v, 1)) break;
        v = settings->freq_step_hz;
        if(!flipper_format_write_uint32(ff, "Freq_step", &v, 1)) break;
        v = settings->ui_style;
        if(!flipper_format_write_uint32(ff, "Ui_style", &v, 1)) break;
        v = settings->ui_feedback;
        if(!flipper_format_write_uint32(ff, "Ui_feedback", &v, 1)) break;
        b = settings->ui_transitions;
        if(!flipper_format_write_bool(ff, "Ui_transitions", &b, 1)) break;
        v = settings->ui_animation;
        if(!flipper_format_write_uint32(ff, "Ui_animation", &v, 1)) break;
        b = settings->assistant;
        if(!flipper_format_write_bool(ff, "Assistant", &b, 1)) break;
        uint32_t look[6];
        for(size_t i = 0; i < 6; i++)
            look[i] = settings->look[i];
        if(!flipper_format_write_uint32(ff, "Dolphin_look", look, 6)) break;
        v = settings->decoration;
        if(!flipper_format_write_uint32(ff, "Lab_decoration", &v, 1)) break;
        v = settings->scan_mode;
        if(!flipper_format_write_uint32(ff, "Scan_mode", &v, 1)) break;
        v = settings->log_interval_s;
        if(!flipper_format_write_uint32(ff, "Log_interval_s", &v, 1)) break;
        v = settings->log_minutes;
        if(!flipper_format_write_uint32(ff, "Log_minutes", &v, 1)) break;
        v = settings->log_max_kb;
        if(!flipper_format_write_uint32(ff, "Log_max_kb", &v, 1)) break;
        ok = true;
    } while(false);
    ok = flipper_format_file_close(ff) && ok;
    flipper_format_free(ff);
    if(ok) {
        FS_Error err =
            storage_common_rename(storage, RADIOGEDDON_SETTINGS_TEMP, RADIOGEDDON_SETTINGS_PATH);
        if(err == FSE_EXIST) {
            // Firmware that does not replace on rename.
            storage_common_remove(storage, RADIOGEDDON_SETTINGS_PATH);
            err = storage_common_rename(
                storage, RADIOGEDDON_SETTINGS_TEMP, RADIOGEDDON_SETTINGS_PATH);
        }
        ok = err == FSE_OK;
    }
    if(!ok) storage_common_remove(storage, RADIOGEDDON_SETTINGS_TEMP);
    return ok;
}
