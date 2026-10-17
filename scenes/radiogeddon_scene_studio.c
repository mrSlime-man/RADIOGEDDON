#include "radiogeddon_scene.h"
#include "../views/radiogeddon_studio_view.h"

#if RG_FEATURE_PULSE_STUDIO

/*
 * Full edition: Pulse Studio for the open RAW capture (app->file_path). The
 * screen and this scene are a module (loaded by the app's forwarder); the
 * analyzer streams the file (never
 * loaded whole), the screen copies the frames' summary from its result and
 * the analyzer is freed. The file stays open while the screen is shown: a
 * frame's samples are read through its checkpoints when the screen asks
 * (one frame at a time, on this thread). Screen, file and module are freed
 * on exit.
 */

typedef enum {
    StudioEventFailed = 980,
    StudioEventLoadBase = 1000, // + frame
} StudioEvent;

#define STUDIO_HEAP_SPARE (6u * 1024u)

static RG_UI_TEXT void studio_cb(uint8_t frame, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, StudioEventLoadBase + frame);
}

static RG_UI_TEXT void studio_fail(RadioGeddonApp* app, const char* text) {
    app->message_header = "Cannot open Studio";
    app->message_text = text;
    view_dispatcher_send_custom_event(app->view_dispatcher, StudioEventFailed);
}

RG_UI_TEXT void radiogeddon_scene_studio_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->studio_file =
        radiogeddon_storage_raw_open(app->storage, furi_string_get_cstr(app->file_path));
    if(!app->studio_file) {
        studio_fail(app, "Could not open\nthe file.");
        return;
    }
    radiogeddon_scene_show_progress(app, "Reading pulses...");
    // This module's copy of the analysis run reports to the same screen.
    radiogeddon_analysis_set_progress(radiogeddon_scene_progress, app);
    RadioGeddonAnalysisStatus status;
    // Only the result is kept past the passes, so the view below is
    // allocated with the analyzer's working state already freed.
    RgAnalysis* a = radiogeddon_analysis_run_raw(app->studio_file, NULL, NULL, &status);
    radiogeddon_analysis_set_progress(NULL, NULL);
    radiogeddon_scene_progress_end(app);
    if(!a ||
       memmgr_heap_get_max_free_block() < radiogeddon_studio_view_memory() + STUDIO_HEAP_SPARE) {
        free(a);
        radiogeddon_storage_raw_close(app->studio_file);
        app->studio_file = NULL;
        studio_fail(
            app,
            status == RadioGeddonAnalysisNoRaw ?
                "No RAW timing data:\nStudio needs a\nRAW capture." :
                "Not enough free\nmemory, or the file\nis damaged.");
        return;
    }
    app->studio_view = radiogeddon_studio_view_alloc();
    radiogeddon_studio_view_set_callback(app->studio_view, studio_cb, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewStudio,
        radiogeddon_studio_view_get_view(app->studio_view));
    radiogeddon_studio_view_set_analysis(
        app->studio_view, furi_string_get_cstr(app->loaded.name), a);
    // The first frame that decodes, read before the analysis goes.
    uint8_t first = 0;
    for(size_t i = 0; i < a->frames_kept; i++) {
        if(a->frames[i].fit >= RG_ANALYZER_GOOD_FIT) {
            first = (uint8_t)i;
            break;
        }
    }
    bool any = a->frames_kept > 0;
    free(a);
    if(any) radiogeddon_studio_view_load_frame(app->studio_view, &app->studio_file->reader, first);
#if RG_FEATURE_JOURNAL
    radiogeddon_research_record(app, RgJrnStudio, 1, furi_string_get_cstr(app->loaded.name));
    radiogeddon_research_step(app, furi_string_get_cstr(app->file_path), RadioGeddonStepStudio, 0);
#endif
    radiogeddon_memdiag_sample("Pulse Studio");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewStudio);
}

RG_UI_TEXT bool radiogeddon_scene_studio_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(!app->studio_view) {
        if(event.event == StudioEventFailed) {
            const char* header = app->message_header;
            const char* text = app->message_text;
            scene_manager_previous_scene(app->scene_manager);
            radiogeddon_scene_show_message(app, header, text);
            return true;
        }
        return false;
    }
    if(event.event >= StudioEventLoadBase && event.event < StudioEventLoadBase + 256u) {
        radiogeddon_studio_view_load_frame(
            app->studio_view,
            &app->studio_file->reader,
            (uint8_t)(event.event - StudioEventLoadBase));
        return true;
    }
    return false;
}

RG_UI_TEXT void radiogeddon_scene_studio_on_exit(void* context) {
    RadioGeddonApp* app = context;
    if(app->studio_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewStudio);
        radiogeddon_studio_view_free(app->studio_view);
        app->studio_view = NULL;
    }
    if(app->studio_file) {
        radiogeddon_storage_raw_close(app->studio_file);
        app->studio_file = NULL;
    }
    radiogeddon_ui_cancel(app);
}

#endif
