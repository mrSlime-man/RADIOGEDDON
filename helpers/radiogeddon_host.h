/**
 * @file radiogeddon_host.h
 * @brief Full edition: what a screen module may call back into the app.
 *
 * A module links against the firmware's API only. The Sessions module's
 * scenes still need a few of the app's own helpers (messages, progress, the
 * Multi-Compare list, loading another module); the app hands it this table
 * when loading it. In a module build (RADIOGEDDON_MODULE defined),
 * radiogeddon_host_map.h maps the helpers' names onto the table, so the
 * scene sources read the same as the app's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <furi.h>
#include <storage/storage.h>
#include "rg_memstat.h"

typedef struct RadioGeddonApp RadioGeddonApp;
typedef struct RadioGeddonScanner RadioGeddonScanner;
typedef struct RadioGeddonSubGhz RadioGeddonSubGhz;
/* radiogeddon_analysis.h's RadioGeddonUnknownFn. */
typedef int (*RadioGeddonHostUnknownFn)(Storage* storage, const char* path, FuriString* out);
typedef void (*RadioGeddonHostScannerHit)(size_t index, void* context);
/* radiogeddon_subghz.h's RadioGeddonSubGhzDecodeCallback. */
typedef void (*RadioGeddonHostDecodeCallback)(
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized,
    void* context);
typedef void (*RadioGeddonHostScannerObserver)(
    void* context,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms);

typedef struct {
    void (*show_message)(RadioGeddonApp* app, const char* header, const char* text);
    void (*show_confirm)(
        RadioGeddonApp* app,
        const char* header,
        const char* text,
        const char* yes_label,
        uint32_t event_yes,
        uint32_t event_no);
    void (*show_busy)(RadioGeddonApp* app, const char* text);
    void (*show_progress)(RadioGeddonApp* app, const char* text);
    void (*progress)(void* context, uint32_t done, uint32_t total);
    void (*progress_end)(RadioGeddonApp* app);
    bool (*module_load)(RadioGeddonApp* app, const char* file, size_t extra_heap);
    void (*module_unload)(RadioGeddonApp* app);
    bool (*multi_add)(RadioGeddonApp* app, const char* path);
    void (*multi_clear)(RadioGeddonApp* app);
    size_t (*multi_memory)(size_t count);
    /* Research Journal: an event (RgJournalEvent) with a note. */
    void (*research_record)(RadioGeddonApp* app, uint32_t event, uint32_t count, const char* note);
    /* The research workflow (helpers/radiogeddon_research.h). */
    void (*research_step)(RadioGeddonApp* app, const char* path, uint32_t step, uint32_t arg);
    void (*research_set_context)(RadioGeddonApp* app, const char* path, bool raw);
    bool (*research_context_path)(RadioGeddonApp* app, FuriString* out);
    void (*research_set_similar)(RadioGeddonApp* app, const char* const* names, size_t n);
    void (*research_session_added)(RadioGeddonApp* app);
    bool (*research_clear)(RadioGeddonApp* app, bool everything);
    void (*research_dismiss_tip)(RadioGeddonApp* app);
    /* Dolphin Lab. */
    void (*ui_event)(RadioGeddonApp* app, uint32_t event, uint32_t arg);
    void (*ui_cancel)(RadioGeddonApp* app);
    void (*ui_set_scanning)(RadioGeddonApp* app, bool scanning);
    void (*ui_apply_style)(RadioGeddonApp* app);
    bool (*ui_active)(RadioGeddonApp* app);
    /* Battery saver: whether a live screen should redraw now. */
    bool (*redraw_due)(RadioGeddonApp* app);
    /* Dolphin Lab badge: RgMascotFrame* frame (radiogeddon_ui_badge). */
    bool (*ui_badge)(RadioGeddonApp* app, void* frame, bool* cyber);
    /* Range Scanner (Full edition): bands of the radio, the planned range. */
    void (*probe_bands)(RadioGeddonApp* app);
    uint8_t (*plan_range)(RadioGeddonApp* app); // RgRangeResult
    /* The app. */
    void (*save_settings)(RadioGeddonApp* app);
    size_t (
        *build_list)(RadioGeddonApp* app, uint8_t source, uint32_t mask, uint32_t* out, size_t max);
    void (*memdiag_sample)(const char* label);
    const char* (*preset_label)(uint8_t index);
    /* The Scanner engine (helpers/radiogeddon_scanner.h). */
    RadioGeddonScanner* (*scanner_alloc)(RadioGeddonSubGhz* subghz);
    void (*scanner_configure)(
        RadioGeddonScanner* s,
        const uint32_t* frequencies,
        size_t count,
        uint16_t dwell_ms,
        uint8_t threshold_db,
        bool hold_on_hit);
    void (*scanner_set_callback)(RadioGeddonScanner* s, RadioGeddonHostScannerHit cb, void* ctx);
    void (*scanner_set_mode)(RadioGeddonScanner* s, uint8_t mode);
    void (
        *scanner_set_observer)(RadioGeddonScanner* s, RadioGeddonHostScannerObserver o, void* ctx);
    void (*scanner_start)(RadioGeddonScanner* s);
    void (*scanner_stop)(RadioGeddonScanner* s);
    void (*scanner_lock)(RadioGeddonScanner* s);
    void (*scanner_unlock)(RadioGeddonScanner* s);
    void (*scanner_reset_learning)(RadioGeddonScanner* s);
    void (*scanner_reset_peaks)(RadioGeddonScanner* s);
    /* The radio and sessions. */
    bool (*subghz_is_device_present)(RadioGeddonSubGhz* subghz);
    void (*subghz_set_preset)(RadioGeddonSubGhz* subghz, uint8_t index);
    void (*subghz_set_frequency)(RadioGeddonSubGhz* subghz, uint32_t frequency);
    bool (*subghz_is_frequency_allowed)(RadioGeddonSubGhz* subghz, uint32_t frequency);
    uint8_t (*subghz_get_radio)(RadioGeddonSubGhz* subghz); // RadioGeddonRadio
    void (*subghz_scan_begin)(RadioGeddonSubGhz* subghz);
    void (*subghz_scan_end)(RadioGeddonSubGhz* subghz);
    float (*subghz_probe_rssi_dwell)(
        RadioGeddonSubGhz* subghz,
        uint32_t frequency,
        uint32_t dwell_ms);
    /* RadioGeddonRadio radio; returns the radio now in use. */
    uint8_t (*subghz_set_radio)(RadioGeddonSubGhz* subghz, uint8_t radio, bool ext_power);
    const char* (*subghz_device_name)(RadioGeddonSubGhz* subghz);
    const char* (*subghz_region_name)(void);
    uint32_t (*settings_default_hop_mask)(void);
    /* Receive sessions and recording (the Hopper). */
    void (*subghz_rx_start)(RadioGeddonSubGhz* subghz, RadioGeddonHostDecodeCallback cb, void* ctx);
    void (*subghz_rx_stop)(RadioGeddonSubGhz* subghz);
    void (*subghz_rx_retune)(RadioGeddonSubGhz* subghz, uint32_t frequency);
    float (*subghz_get_rssi)(RadioGeddonSubGhz* subghz);
    uint8_t (*subghz_record_start)(RadioGeddonSubGhz* subghz); // RadioGeddonRecordError
    void (*subghz_record_stop)(RadioGeddonSubGhz* subghz);
    bool (*subghz_record_status)(RadioGeddonSubGhz* subghz, void* stats); // RadioGeddonRecordStats
    bool (*subghz_record_pending)(RadioGeddonSubGhz* subghz);
    bool (*subghz_record_save)(RadioGeddonSubGhz* subghz, const char* path);
    void (*subghz_record_discard)(RadioGeddonSubGhz* subghz);
    const char* (*recorder_error_text)(uint8_t error); // RadioGeddonRecordError
    bool (*radio_memory_ok)(RadioGeddonApp* app);
    void (*radio_memory_learn)(RadioGeddonApp* app);
    void (*receiver_badge)(RadioGeddonApp* app);
    bool (*session_capture_saved)(Storage* storage, const char* file, char* out, size_t size);
    void (*session_active)(Storage* storage, char* out, size_t size);
    /* Reports (the reports module). */
    void (*show_report)(RadioGeddonApp* app, const char* title, const char* text);
    RadioGeddonHostUnknownFn (*unknown_begin)(void* context);
    void (*unknown_end)(void* context);
    bool (*decoders_fit)(RadioGeddonApp* app);
    void (*analysis_decode)(
        Storage* storage,
        RadioGeddonSubGhz* subghz,
        const char* path,
        FuriString* out);
    /* About and its diagnostics report (the about module). */
    const RgMemStat* (*memdiag_stats)(void);
    const RgDiag* (*memdiag_diags)(void);
    uint32_t (*subghz_session_cost)(RadioGeddonSubGhz* subghz);
    uint32_t (*radio_cost)(RadioGeddonApp* app);
    /* The shared VariableItemList, emptied with a resident enter callback. */
    void (*var_list_release)(RadioGeddonApp* app);
    /* The Database (the database module). */
    void (*db_release)(RadioGeddonApp* app);
    void (*sessions_rename_signal)(Storage* storage, const char* from, const char* to);
    /* Favorites (the settings module) and the analysis menu (the analyze
     * module). The favorites are radiogeddon_profiles.h's RadioGeddonFavorites. */
    void* (*favorites)(RadioGeddonApp* app);
    bool (*favorites_save)(Storage* storage, const void* favorites);
    bool (*decoders_memory_ok)(RadioGeddonApp* app);
} RadioGeddonHost;

/** The table (scenes/radiogeddon_scene_tools.c). */
extern const RadioGeddonHost radiogeddon_host_table;
