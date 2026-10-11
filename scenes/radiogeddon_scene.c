#include "radiogeddon_scene.h"
#include "../helpers/rg_memstat.h"

#define TAG "RadioGeddonScene"

// Collect handler function pointers into the three tables.
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const radiogeddon_scene_on_enter_handlers[])(void*) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const radiogeddon_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const radiogeddon_scene_on_exit_handlers[])(void*) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers radiogeddon_scene_handlers = {
    .on_enter_handlers = radiogeddon_scene_on_enter_handlers,
    .on_event_handlers = radiogeddon_scene_on_event_handlers,
    .on_exit_handlers = radiogeddon_scene_on_exit_handlers,
    .scene_num = RadioGeddonSceneNum,
};

static uint32_t radiogeddon_confirm_yes;
static uint32_t radiogeddon_confirm_no;

static void radiogeddon_scene_confirm_cb(GuiButtonType result, InputType type, void* context) {
    RadioGeddonApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, radiogeddon_confirm_no);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, radiogeddon_confirm_yes);
    }
}

void radiogeddon_scene_show_confirm(
    RadioGeddonApp* app,
    const char* header,
    const char* text,
    const char* yes_label,
    uint32_t event_yes,
    uint32_t event_no) {
    radiogeddon_confirm_yes = event_yes;
    radiogeddon_confirm_no = event_no;
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_string_element(widget, 64, 4, AlignCenter, AlignTop, FontPrimary, header);
    furi_string_set(app->temp_str, text);
    widget_add_text_box_element(
        widget,
        0,
        18,
        128,
        30,
        AlignCenter,
        AlignCenter,
        furi_string_get_cstr(app->temp_str),
        false);
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Cancel", radiogeddon_scene_confirm_cb, app);
    widget_add_button_element(
        widget, GuiButtonTypeRight, yes_label, radiogeddon_scene_confirm_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

void radiogeddon_scene_show_busy(RadioGeddonApp* app, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, text, 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

/* Redraw the percentage at most this often. */
#define RADIOGEDDON_PROGRESS_MIN_MS 150u
#define RADIOGEDDON_PROGRESS_NONE   0xFFu

void radiogeddon_scene_show_progress(RadioGeddonApp* app, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, text, 64, 24, AlignCenter, AlignCenter);
    radiogeddon_memdiag_sample(text);
    app->progress_pct = RADIOGEDDON_PROGRESS_NONE;
    app->progress_tick = 0;
    radiogeddon_analysis_set_progress(radiogeddon_scene_progress, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

void radiogeddon_scene_progress(void* context, uint32_t done, uint32_t total) {
    RadioGeddonApp* app = context;
    radiogeddon_memdiag_sample(NULL); // the work in progress is at its largest now
    if(total == 0) return;
    uint32_t pct = (uint32_t)((uint64_t)done * 100u / total);
    if(pct > 100) pct = 100;
    uint32_t now = furi_get_tick();
    if(pct == app->progress_pct) return;
    if(app->progress_pct != RADIOGEDDON_PROGRESS_NONE &&
       now - app->progress_tick < furi_ms_to_ticks(RADIOGEDDON_PROGRESS_MIN_MS))
        return;
    app->progress_pct = (uint8_t)pct;
    app->progress_tick = now;
    app->progress_slot ^= 1;
    char* text = app->progress_text[app->progress_slot];
    snprintf(text, sizeof(app->progress_text[0]), "%lu%%", (unsigned long)pct);
    popup_set_text(app->popup, text, 64, 42, AlignCenter, AlignCenter);
}

void radiogeddon_scene_progress_end(RadioGeddonApp* app) {
    radiogeddon_analysis_set_progress(NULL, NULL);
    radiogeddon_memdiag_sample(NULL);
    UNUSED(app);
}

/* Free heap kept on top of a receive session's measured cost (history, texts,
 * screens and other threads' use while it runs). */
#define RADIOGEDDON_RADIO_MARGIN (6u * 1024u)

uint32_t radiogeddon_scene_radio_cost(RadioGeddonApp* app) {
    return app->settings.radio_heap_fw == app->fw_tag ? app->settings.radio_heap : 0;
}

bool radiogeddon_scene_decoders_fit(RadioGeddonApp* app) {
    return rg_mem_session_fits(
        radiogeddon_memdiag_free(), radiogeddon_scene_radio_cost(app), RADIOGEDDON_RADIO_MARGIN);
}

/* @p what names the user ("Radio needs") in the refusal. */
static bool radiogeddon_scene_session_memory_ok(RadioGeddonApp* app, const char* what) {
    uint32_t cost = radiogeddon_scene_radio_cost(app);
    uint32_t free_now = radiogeddon_memdiag_free();
    if(rg_mem_session_fits(free_now, cost, RADIOGEDDON_RADIO_MARGIN)) return true;

    FURI_LOG_W(
        TAG,
        "Radio not started: %lu free, session took %lu",
        (unsigned long)free_now,
        (unsigned long)cost);
    snprintf(
        app->memory_text,
        sizeof(app->memory_text),
        "%s ~%lu KB,\n%lu KB free. Restart\nthe Flipper and retry.",
        what,
        (unsigned long)((cost + RADIOGEDDON_RADIO_MARGIN + 1023) / 1024),
        (unsigned long)(free_now / 1024));
    popup_reset(app->popup);
    popup_set_header(app->popup, "Not enough memory", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->memory_text, 64, 39, AlignCenter, AlignCenter);
    notification_message(app->notifications, &sequence_error);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
    return false;
}

bool radiogeddon_scene_radio_memory_ok(RadioGeddonApp* app) {
    return radiogeddon_scene_session_memory_ok(app, "Radio needs");
}

/* The decoders and keystore are most of a receive session's cost, so its
 * measurement is a safe bound for decoding a file without the radio. */
bool radiogeddon_scene_decoders_memory_ok(RadioGeddonApp* app) {
    return radiogeddon_scene_session_memory_ok(app, "Decoders need");
}

void radiogeddon_scene_radio_memory_learn(RadioGeddonApp* app) {
    uint32_t cost = radiogeddon_subghz_session_cost(app->subghz);
    if(!rg_mem_cost_changed(radiogeddon_scene_radio_cost(app), cost)) return;
    app->settings.radio_heap = cost;
    app->settings.radio_heap_fw = app->fw_tag;
    // Only this changes; the stored frequency and preset stay as they were.
    radiogeddon_settings_save(app->storage, &app->settings);
}

void radiogeddon_scene_show_message(RadioGeddonApp* app, const char* header, const char* text) {
    app->message_header = header;
    app->message_text = text;
    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMessage);
}

/* The analysis streams the file through the analyzer (sizeof(RgAnalyzer)) and
 * grows a report text of a few kilobytes. */
#define RADIOGEDDON_UNKNOWN_WORK (sizeof(RgAnalyzer) + 10u * 1024u)

RadioGeddonUnknownFn radiogeddon_scene_unknown_begin(void* context) {
#if RG_EDITION_FULL
    RadioGeddonApp* app = context;
    if(!radiogeddon_scene_module_load(app, RADIOGEDDON_MODULE_UNKNOWN, RADIOGEDDON_UNKNOWN_WORK))
        return NULL;
    return ((const RadioGeddonUnknownModule*)app->module_api)->unknown;
#else
    UNUSED(context);
    return radiogeddon_analysis_unknown;
#endif
}

void radiogeddon_scene_unknown_end(void* context) {
#if RG_EDITION_FULL
    RadioGeddonApp* app = context;
    if(app->module) radiogeddon_scene_module_unload(app);
#else
    UNUSED(context);
#endif
}

void radiogeddon_scene_db_release(RadioGeddonApp* app) {
    if(app->db_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewDb);
        radiogeddon_db_view_free(app->db_view);
        app->db_view = NULL;
    }
    radiogeddon_db_free(app->db);
    app->db = NULL;
    app->db_keep = false;
    app->db_dirty = false;
}

#if RG_EDITION_FULL
bool radiogeddon_scene_module_load(RadioGeddonApp* app, const char* file, size_t extra_heap) {
    furi_check(!app->module);
    RadioGeddonModuleStatus status;
    app->module =
        radiogeddon_module_load(app->storage, file, extra_heap, &app->module_api, &status);
    if(app->module) {
        radiogeddon_memdiag_sample(file);
        return true;
    }
    radiogeddon_module_error(status, &app->message_header, &app->message_text);
    return false;
}

void radiogeddon_scene_module_unload(RadioGeddonApp* app) {
    radiogeddon_module_unload(app->module);
    app->module = NULL;
    app->module_api = NULL;
}

RadioGeddonFavorites* radiogeddon_scene_favorites(RadioGeddonApp* app) {
    if(!app->favorites_loaded) {
        radiogeddon_favorites_load(app->storage, &app->favorites);
        app->favorites_loaded = true;
    }
    return &app->favorites;
}

void radiogeddon_scene_probe_bands(RadioGeddonApp* app) {
    radiogeddon_subghz_probe_bands(app->subghz, &app->bands);
}

RgRangeResult radiogeddon_scene_plan_range(RadioGeddonApp* app) {
    app->range_result = rg_range_plan(
        &app->range,
        app->settings.range_start_hz,
        app->settings.range_end_hz,
        app->settings.range_step_hz,
        &app->bands,
        RG_RANGE_MAX_POINTS);
    return app->range_result;
}
#endif

size_t radiogeddon_scene_build_list(
    RadioGeddonApp* app,
    uint8_t source,
    uint32_t mask,
    uint32_t* out,
    size_t max) {
    size_t n = 0;
#if RG_FEATURE_FAVORITES
    if(source == RadioGeddonSourceFavorites) {
        const RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
        for(size_t i = 0; i < fav->count && n < max; i++) {
            if(radiogeddon_subghz_is_frequency_allowed(app->subghz, fav->freq[i])) {
                out[n++] = fav->freq[i];
            }
        }
        return n;
    }
#else
    UNUSED(source); // only the built-in list in this edition
#endif
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32 && n < max; i++) {
        if(!(mask & (1u << i))) continue;
        uint32_t f = radiogeddon_frequencies[i];
        if(radiogeddon_subghz_is_frequency_allowed(app->subghz, f)) out[n++] = f;
    }
    return n;
}
