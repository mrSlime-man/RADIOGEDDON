#include "radiogeddon_scene.h"

// Save the open file's analysis reports to the SD card as text.

#define REPORT_RESULT_MS 3000u

typedef enum {
    ReportEventDone = 700,
} ReportEvent;

static void radiogeddon_scene_report_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ReportEventDone);
}

void radiogeddon_scene_report_on_enter(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_scene_show_progress(app, "Writing report...");

    FuriString* path = furi_string_alloc();
    // The decoders run only if they fit; the report says when they did not.
    RadioGeddonReportResult result = radiogeddon_report_save(
        app->storage,
        radiogeddon_scene_decoders_fit(app) ? app->subghz : NULL,
        furi_string_get_cstr(app->file_path),
        &app->loaded,
        &(RadioGeddonUnknownProvider){
            radiogeddon_scene_unknown_begin, radiogeddon_scene_unknown_end, app},
        app->temp_str,
        path);
    radiogeddon_scene_progress_end(app);

    furi_string_reset(app->temp_str);
    if(result == RadioGeddonReportOk) {
        notification_message(app->notifications, &sequence_success);
        const char* full = furi_string_get_cstr(path);
        const char* slash = strrchr(full, '/');
        furi_string_printf(app->temp_str, "reports/%s", slash ? slash + 1 : full);
    } else {
        notification_message(app->notifications, &sequence_error);
        furi_string_set(app->temp_str, radiogeddon_report_result_text(result));
    }
    furi_string_free(path);

    popup_reset(app->popup);
    popup_set_header(
        app->popup,
        result == RadioGeddonReportOk ? "Report saved" : "Report not saved",
        64,
        14,
        AlignCenter,
        AlignCenter);
    popup_set_text(
        app->popup, furi_string_get_cstr(app->temp_str), 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_report_popup_cb);
    popup_set_timeout(app->popup, REPORT_RESULT_MS);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

bool radiogeddon_scene_report_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == ReportEventDone) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void radiogeddon_scene_report_on_exit(void* context) {
    RadioGeddonApp* app = context;
    popup_reset(app->popup);
    furi_string_reset(app->temp_str);
}
