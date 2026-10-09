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

bool radiogeddon_scene_radio_memory_ok(RadioGeddonApp* app) {
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
        "Radio needs ~%lu KB,\n%lu KB free. Restart\nthe Flipper and retry.",
        (unsigned long)((cost + RADIOGEDDON_RADIO_MARGIN + 1023) / 1024),
        (unsigned long)(free_now / 1024));
    popup_reset(app->popup);
    popup_set_header(app->popup, "Not enough memory", 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, app->memory_text, 64, 39, AlignCenter, AlignCenter);
    notification_message(app->notifications, &sequence_error);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
    return false;
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
