#include "radiogeddon_scene.h"
#include "../helpers/radiogeddon_actlog.h"

#if RG_FEATURE_ACTIVITY_LOG

/*
 * Full edition: RF Activity Logger. Setup, then a passive observation of the
 * Scanner's frequency list by the Scanner engine (Standard or an adaptive
 * schedule), logged by this module (radiogeddon_log.fal, with the files'
 * code): every visit is counted, and on
 * the tick the interval's rows are collected under the scanner's lock and
 * written without it. The observation ends when its time is up, when Back
 * is pressed, when the log cannot be written, or when this screen closes
 * for any reason: nothing keeps listening after it. The summary is shown
 * and saved next to the log.
 *
 * Scene state: ActlogModeSetup, ActlogModeRunning or ActlogModeSummary.
 */

typedef enum {
    ActlogModeSetup,
    ActlogModeRunning,
    ActlogModeSummary,
} ActlogMode;

typedef enum {
    ActlogEventStart = 1300,
    ActlogEventFailed,
} ActlogEvent;

typedef enum {
    ActlogRowMode,
    ActlogRowDuration,
    ActlogRowInterval,
    ActlogRowSize,
    ActlogRowThreshold,
    ActlogRowList,
    ActlogRowStart,
} ActlogRow;

#define ACTLOG_SUMMARY_SIZE 900u
#define ACTLOG_ROWS_SIZE    (RG_ACTLOG_MAX_CHANNELS * RG_ACTLOG_LINE_MAX)

typedef struct {
    RadioGeddonActlog* log;
    char* rows;
    char* summary;
    uint32_t last_draw;
    bool stopped; // the log stopped itself (full, failed, time up)
} ActlogRun;

static const uint16_t actlog_minutes[] = {15, 30, 60, 120, 240, 480, 0};
static const char* const actlog_minutes_text[] =
    {"15 min", "30 min", "1 h", "2 h", "4 h", "8 h", "Until stop"};
static const uint16_t actlog_intervals[] = {10, 30, 60, 300};
static const char* const actlog_intervals_text[] = {"10 s", "30 s", "1 min", "5 min"};
static const uint16_t actlog_sizes[] = {64, 256, 1024};
static const char* const actlog_sizes_text[] = {"64 KB", "256 KB", "1 MB"};
static const RgAdaptMode actlog_modes[] = {RgAdaptStandard, RgAdaptBalanced, RgAdaptFocused};

static ActlogRun* actlog_run(RadioGeddonApp* app) {
    return app->actlog;
}

static uint8_t actlog_find16(const uint16_t* v, size_t n, uint16_t x) {
    for(size_t i = 0; i < n; i++)
        if(v[i] == x) return (uint8_t)i;
    return 0;
}

static void actlog_set(VariableItem* item, const char* text) {
    variable_item_set_current_value_text(item, text);
}

static void actlog_mode_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.scan_mode = (uint8_t)actlog_modes[i];
    actlog_set(item, rg_adapt_mode_name(actlog_modes[i]));
}

static void actlog_duration_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.log_minutes = actlog_minutes[i];
    actlog_set(item, actlog_minutes_text[i]);
}

static void actlog_interval_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.log_interval_s = actlog_intervals[i];
    actlog_set(item, actlog_intervals_text[i]);
}

static void actlog_size_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.log_max_kb = actlog_sizes[i];
    actlog_set(item, actlog_sizes_text[i]);
}

static void actlog_threshold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.scan_threshold_db = (uint8_t)(6u + 2u * i);
    char text[8];
    snprintf(text, sizeof(text), "+%u dB", (unsigned)app->settings.scan_threshold_db);
    actlog_set(item, text);
}

static void actlog_enter(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    if(index == ActlogRowStart)
        view_dispatcher_send_custom_event(app->view_dispatcher, ActlogEventStart);
}

static size_t actlog_list(RadioGeddonApp* app, uint32_t* list) {
    return radiogeddon_scene_build_list(
        app,
        app->settings.scan_source,
        app->settings.scan_mask,
        list,
        RADIOGEDDON_SCANNER_MAX_CHANNELS);
}

static void actlog_show_setup(RadioGeddonApp* app) {
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    RadioGeddonSettings* s = &app->settings;
    uint8_t mi = 0;
    for(uint8_t i = 0; i < COUNT_OF(actlog_modes); i++)
        if(actlog_modes[i] == s->scan_mode) mi = i;
    s->scan_mode = (uint8_t)actlog_modes[mi];
    VariableItem* item =
        variable_item_list_add(list, "Schedule", COUNT_OF(actlog_modes), actlog_mode_changed, app);
    variable_item_set_current_value_index(item, mi);
    actlog_set(item, rg_adapt_mode_name(actlog_modes[mi]));

    uint8_t di = actlog_find16(actlog_minutes, COUNT_OF(actlog_minutes), s->log_minutes);
    item = variable_item_list_add(
        list, "Duration", COUNT_OF(actlog_minutes), actlog_duration_changed, app);
    variable_item_set_current_value_index(item, di);
    actlog_set(item, actlog_minutes_text[di]);
    s->log_minutes = actlog_minutes[di];

    uint8_t ii = actlog_find16(actlog_intervals, COUNT_OF(actlog_intervals), s->log_interval_s);
    item = variable_item_list_add(
        list, "Log every", COUNT_OF(actlog_intervals), actlog_interval_changed, app);
    variable_item_set_current_value_index(item, ii);
    actlog_set(item, actlog_intervals_text[ii]);
    s->log_interval_s = actlog_intervals[ii];

    uint8_t si = actlog_find16(actlog_sizes, COUNT_OF(actlog_sizes), s->log_max_kb);
    item = variable_item_list_add(
        list, "File part", COUNT_OF(actlog_sizes), actlog_size_changed, app);
    variable_item_set_current_value_index(item, si);
    actlog_set(item, actlog_sizes_text[si]);
    s->log_max_kb = actlog_sizes[si];

    uint8_t ti = s->scan_threshold_db < 6 ? 0 : (uint8_t)((s->scan_threshold_db - 6u) / 2u);
    if(ti > 7) ti = 7;
    item = variable_item_list_add(list, "Threshold", 8, actlog_threshold_changed, app);
    variable_item_set_current_value_index(item, ti);
    actlog_threshold_changed(item);

    uint32_t freqs[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    size_t n = actlog_list(app, freqs);
    item = variable_item_list_add(list, "Frequencies", 1, NULL, app);
    char text[16];
    snprintf(text, sizeof(text), "%u (Scanner)", (unsigned)n);
    actlog_set(item, text);
    variable_item_list_add(list, "Start logging", 1, NULL, app);
    variable_item_list_set_enter_callback(list, actlog_enter, app);
    variable_item_list_set_selected_item(list, ActlogRowStart);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

static void actlog_fmt_time(char* out, size_t size, uint32_t s) {
    snprintf(
        out,
        size,
        "%lu:%02lu:%02lu",
        (unsigned long)(s / 3600u),
        (unsigned long)(s / 60u % 60u),
        (unsigned long)(s % 60u));
}

static void actlog_draw_running(RadioGeddonApp* app) {
    ActlogRun* run = actlog_run(app);
    RadioGeddonActlogStatus st;
    radiogeddon_scanner_lock(app->scanner);
    radiogeddon_actlog_status(run->log, furi_get_tick(), &st);
    radiogeddon_scanner_unlock(app->scanner);
    char t1[16], t2[16];
    actlog_fmt_time(t1, sizeof(t1), st.elapsed_s);
    actlog_fmt_time(t2, sizeof(t2), st.limit_s);
    furi_string_printf(
        app->temp_str,
        "Logging %s / %s\n%lu events, %lu rows, %lu KB\n",
        t1,
        st.limit_s ? t2 : "until Back",
        (unsigned long)st.events,
        (unsigned long)st.rows,
        (unsigned long)(st.bytes / 1024u));
    for(size_t k = 0; k < 2; k++) {
        if(st.top[k] == 0xFF) continue;
        furi_string_cat_printf(
            app->temp_str,
            "%lu.%02lu: %lu ev, peak %d\n",
            (unsigned long)(st.top_hz[k] / 1000000u),
            (unsigned long)(st.top_hz[k] / 10000u % 100u),
            (unsigned long)st.top_events[k],
            (int)st.top_peak[k]);
    }
    furi_string_cat_str(app->temp_str, "One freq. at a time. Back: stop");
    widget_reset(app->widget);
    widget_add_text_box_element(
        app->widget, 0, 0, 128, 64, AlignLeft, AlignTop, furi_string_get_cstr(app->temp_str), false);
}

static void actlog_free_run(RadioGeddonApp* app) {
    ActlogRun* run = actlog_run(app);
    if(!run) return;
    free(run->rows);
    free(run->summary);
    free(run);
    app->actlog = NULL;
}

/* Stop listening and close the log; the summary is then shown. */
static void actlog_stop(RadioGeddonApp* app, const char* reason) {
    ActlogRun* run = actlog_run(app);
    if(!run || !run->log) return;
    radiogeddon_scanner_stop(app->scanner);
    radiogeddon_scanner_set_observer(app->scanner, NULL, NULL);
    radiogeddon_ui_set_scanning(app, false);
    radiogeddon_actlog_finish(
        run->log, reason, furi_get_tick(), run->summary, ACTLOG_SUMMARY_SIZE);
    run->log = NULL;
#if RG_FEATURE_JOURNAL
    radiogeddon_research_record(app, RgJrnActlog, 1, NULL);
#endif
    radiogeddon_ui_event(app, RgMascotEventMilestone, 0);
    notification_message(app->notifications, &sequence_success);
}

static void actlog_show_summary(RadioGeddonApp* app) {
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneActlog, ActlogModeSummary);
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, actlog_run(app)->summary);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

static bool actlog_start(RadioGeddonApp* app) {
    radiogeddon_app_save_settings(app);
    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        app->message_header = "No radio";
        app->message_text = "Sub-GHz device not\nfound or not responding.";
        return false;
    }
    uint32_t freqs[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    size_t n = actlog_list(app, freqs);
    if(n == 0) {
        app->message_header = "No frequencies";
        app->message_text = "Choose some in Settings\n> Scanner frequencies.";
        return false;
    }
    if(memmgr_heap_get_max_free_block() <
       radiogeddon_actlog_memory(n) + ACTLOG_ROWS_SIZE + ACTLOG_SUMMARY_SIZE + 4096u) {
        app->message_header = "Not enough memory";
        app->message_text = "Close other tools or\nuse Classic mode.";
        return false;
    }
    RadioGeddonActlogConfig cfg = {
        .interval_s = app->settings.log_interval_s,
        .minutes = app->settings.log_minutes,
        .max_bytes = (uint32_t)app->settings.log_max_kb * 1024u,
    };
    snprintf(
        cfg.scanner,
        sizeof(cfg.scanner),
        "%s, dwell %u ms, +%u dB, %s",
        RADIOGEDDON_PRESET_LABEL(app->preset_index),
        (unsigned)app->settings.scan_dwell_ms,
        (unsigned)app->settings.scan_threshold_db,
        rg_adapt_mode_name((RgAdaptMode)app->settings.scan_mode));
    ActlogRun* run = malloc(sizeof(ActlogRun));
    memset(run, 0, sizeof(*run));
    run->rows = malloc(ACTLOG_ROWS_SIZE);
    run->summary = malloc(ACTLOG_SUMMARY_SIZE);
    run->summary[0] = '\0';
    app->actlog = run;
    run->log = radiogeddon_actlog_start(app->storage, freqs, n, &cfg, furi_get_tick());
    if(!run->log) {
        actlog_free_run(app);
        app->message_header = "Cannot log";
        app->message_text = "The SD card refused\nthe log file.";
        return false;
    }
    if(!app->scanner) app->scanner = radiogeddon_scanner_alloc(app->subghz);
    radiogeddon_scanner_set_mode(app->scanner, app->settings.scan_mode);
    radiogeddon_scanner_configure(
        app->scanner,
        freqs,
        n,
        app->settings.scan_dwell_ms,
        app->settings.scan_threshold_db,
        false);
    radiogeddon_scanner_set_callback(app->scanner, NULL, NULL);
    radiogeddon_scanner_set_observer(app->scanner, radiogeddon_actlog_observe, run->log);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_scanner_start(app->scanner);
    radiogeddon_ui_set_scanning(app, true);
    radiogeddon_memdiag_sample("Activity Logger");
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneActlog, ActlogModeRunning);
    actlog_draw_running(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
    return true;
}

void radiogeddon_scene_actlog_on_enter(void* context) {
    RadioGeddonApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneActlog, ActlogModeSetup);
    actlog_show_setup(app);
}

/* The tick while logging: rows due, the screen once a second, the end. */
static void actlog_tick(RadioGeddonApp* app) {
    ActlogRun* run = actlog_run(app);
    if(!run || !run->log) return;
    uint32_t now = furi_get_tick();
    radiogeddon_scanner_lock(app->scanner);
    size_t len = radiogeddon_actlog_collect(run->log, now, run->rows, ACTLOG_ROWS_SIZE);
    RadioGeddonActlogStatus st;
    radiogeddon_actlog_status(run->log, now, &st);
    radiogeddon_scanner_unlock(app->scanner);
    bool alive = radiogeddon_actlog_write(run->log, run->rows, len); // never under the lock
    if(!alive || st.state != RadioGeddonActlogRunning) {
        const char* reason = st.state == RadioGeddonActlogDone ? "duration reached" :
                             st.state == RadioGeddonActlogFull ? "size limit reached" :
                                                                 "SD write failed";
        if(!alive && st.state == RadioGeddonActlogRunning) reason = "SD write failed";
        actlog_stop(app, reason);
        actlog_show_summary(app);
        return;
    }
    if(now - run->last_draw >= 1000u) {
        run->last_draw = now;
        actlog_draw_running(app);
    }
}

bool radiogeddon_scene_actlog_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    uint32_t mode = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneActlog);
    if(event.type == SceneManagerEventTypeTick) {
        if(mode == ActlogModeRunning) actlog_tick(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        if(mode == ActlogModeRunning) {
            actlog_stop(app, "stopped by the user");
            actlog_show_summary(app);
            return true;
        }
        return false; // setup or summary: leave
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == ActlogEventStart && mode == ActlogModeSetup) {
        if(!actlog_start(app))
            radiogeddon_scene_show_message(app, app->message_header, app->message_text);
        return true;
    }
    return false;
}

void radiogeddon_scene_actlog_on_exit(void* context) {
    RadioGeddonApp* app = context;
    // Whatever closes this screen ends the observation.
    actlog_stop(app, "screen closed");
    actlog_free_run(app);
    if(app->scanner) radiogeddon_scanner_set_observer(app->scanner, NULL, NULL);
    radiogeddon_app_save_settings(app);
    variable_item_list_set_enter_callback(app->var_item_list, NULL, NULL);
    variable_item_list_reset(app->var_item_list);
    widget_reset(app->widget);
    text_box_reset(app->text_box);
}

#endif
