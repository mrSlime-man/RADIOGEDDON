#include "radiogeddon_scene.h"
#include "../helpers/radiogeddon_heatmap.h"
#include "../helpers/radiogeddon_actlog.h"

#if RG_FEATURE_ACTIVITY_LOG

/*
 * Full edition: RF Activity Heatmap of a saved activity log (a module,
 * radiogeddon_heatmap.fal, separate from the logger's so a long log does not
 * carry it). Opens on the file browser in the activity folder; the chosen
 * log (all its parts) is read into a grid (rg_heatmap.h) and shown
 * (radiogeddon_heatmap_view.h): frequency down, time across, a frequency
 * nobody listened to told apart from one that was listened to and quiet.
 * Back leaves; map, grid and view are freed on exit.
 */

typedef enum {
    HeatmapEventFailed = 1400,
} HeatmapEvent;

typedef struct {
    RgHeatmap map;
    RgHeatCell* cells;
    RadioGeddonHeatmapView* view;
} HeatmapRun;

/* Free heap kept beyond the grid and the view. */
#define HEATMAP_SPARE (4u * 1024u)

static void heatmap_fail(RadioGeddonApp* app, const char* header, const char* text) {
    app->message_header = header;
    app->message_text = text;
    view_dispatcher_send_custom_event(app->view_dispatcher, HeatmapEventFailed);
}

void radiogeddon_scene_heatmap_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->message_header = NULL;
    FuriString* path = furi_string_alloc_set(RADIOGEDDON_ACTLOG_FOLDER);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".csv", NULL);
    options.base_path = RADIOGEDDON_ACTLOG_FOLDER;
    options.hide_ext = false;
    bool chosen = dialog_file_browser_show(app->dialogs, path, path, &options);
    if(!chosen) {
        furi_string_free(path);
        view_dispatcher_send_custom_event(app->view_dispatcher, HeatmapEventFailed);
        return;
    }
    HeatmapRun* run = malloc(sizeof(HeatmapRun));
    memset(run, 0, sizeof(*run));
    app->heatmap = run;
    radiogeddon_scene_show_progress(app, "Reading log...");
    RadioGeddonHeatmapStatus st = radiogeddon_actlog_heatmap(
        app->storage,
        furi_string_get_cstr(path),
        &run->map,
        &run->cells,
        radiogeddon_heatmap_view_memory() + HEATMAP_SPARE);
    radiogeddon_scene_progress_end(app);
    furi_string_free(path);
    if(st != RadioGeddonHeatmapOk) {
        heatmap_fail(
            app,
            "Cannot show the log",
            st == RadioGeddonHeatmapNoMemory ? "Not enough memory\nfor its map." :
            st == RadioGeddonHeatmapNoRows   ? "It holds no activity\nlog rows." :
                                               "The file could not\nbe opened.");
        return;
    }
    run->view = radiogeddon_heatmap_view_alloc();
    radiogeddon_heatmap_view_set_map(run->view, &run->map);
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewHeatmap, radiogeddon_heatmap_view_get_view(run->view));
    radiogeddon_memdiag_sample("Activity heatmap");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewHeatmap);
}

bool radiogeddon_scene_heatmap_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == HeatmapEventFailed) {
        // Leave first, then explain (nothing to say when no log was chosen).
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        if(header) radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    return false;
}

void radiogeddon_scene_heatmap_on_exit(void* context) {
    RadioGeddonApp* app = context;
    HeatmapRun* run = app->heatmap;
    if(!run) return;
    if(run->view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewHeatmap);
        radiogeddon_heatmap_view_free(run->view);
    }
    free(run->cells);
    free(run);
    app->heatmap = NULL;
}

#endif
