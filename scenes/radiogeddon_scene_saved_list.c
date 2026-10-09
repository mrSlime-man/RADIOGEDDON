#include "radiogeddon_scene.h"

// Signal Database list. The index is built when the list opens and kept while
// a file or the options opened from it are shown; it is rebuilt after files
// change (db_dirty) and freed on returning to the main menu.

/* Show "Opening..." for RAW files larger than this. */
#define SAVED_LIST_BUSY_BYTES (32u * 1024u)

typedef enum {
    SavedListEventOpen = 400,
    SavedListEventDetails,
    SavedListEventSort,
    SavedListEventOptions,
    SavedListEventNoMemory,
} SavedListEvent;

static void radiogeddon_scene_saved_list_view_cb(RadioGeddonDbViewEvent event, void* context) {
    RadioGeddonApp* app = context;
    uint32_t out = event == RadioGeddonDbViewEventOpen    ? SavedListEventOpen :
                   event == RadioGeddonDbViewEventDetails ? SavedListEventDetails :
                   event == RadioGeddonDbViewEventSort    ? SavedListEventSort :
                                                            SavedListEventOptions;
    view_dispatcher_send_custom_event(app->view_dispatcher, out);
}

/* Position of the file called @p name in the current view, if listed. */
static bool radiogeddon_scene_saved_list_find(RadioGeddonDb* db, const char* name, size_t* pos) {
    for(size_t i = 0; i < db->view_count; i++) {
        if(strcmp(rg_db_name(&db->db, radiogeddon_db_at(db, i)), name) == 0) {
            *pos = i;
            return true;
        }
    }
    return false;
}

/* (Re)build the index, keeping the query and the highlighted file: the same
 * name, else the file just renamed, else the same position. */
static bool radiogeddon_scene_saved_list_load(RadioGeddonApp* app) {
    RgDbQuery query = {.sort = (RgDbSort)app->settings.db_sort, .show = RgDbShowAll};
    size_t selected = 0;
    FuriString* keep = furi_string_alloc();
    if(app->db) {
        query = app->db->query;
        selected = radiogeddon_db_view_get_selected(app->db_view);
        const RgDbEntry* e = radiogeddon_db_at(app->db, selected);
        if(e) furi_string_set(keep, rg_db_name(&app->db->db, e));
    }
    radiogeddon_scene_show_progress(app, "Reading files...");
    // Detach before freeing so the list never draws a freed index.
    radiogeddon_db_view_set_db(app->db_view, NULL);
    radiogeddon_db_free(app->db);
    app->db = NULL;
    app->db_dirty = false;

    RadioGeddonDbStatus status;
    app->db = radiogeddon_db_load(app->storage, &status, radiogeddon_scene_progress, app);
    radiogeddon_scene_progress_end(app);
    if(app->db) {
        app->db->query = query;
        radiogeddon_db_apply(app->db);
        const char* opened = strrchr(furi_string_get_cstr(app->file_path), '/');
        if(!radiogeddon_scene_saved_list_find(app->db, furi_string_get_cstr(keep), &selected) &&
           opened) {
            radiogeddon_scene_saved_list_find(app->db, opened + 1, &selected);
        }
        radiogeddon_db_view_set_db(app->db_view, app->db);
        radiogeddon_db_view_set_selected(app->db_view, selected);
    }
    furi_string_free(keep);
    return app->db != NULL;
}

void radiogeddon_scene_saved_list_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->db_keep = false;
    if(!app->db_view) {
        app->db_view = radiogeddon_db_view_alloc();
        radiogeddon_db_view_set_callback(app->db_view, radiogeddon_scene_saved_list_view_cb, app);
        view_dispatcher_add_view(
            app->view_dispatcher, RadioGeddonViewDb, radiogeddon_db_view_get_view(app->db_view));
    }

    if(!app->db || app->db_dirty) {
        if(!radiogeddon_scene_saved_list_load(app)) {
            // Leave from the event loop, not from inside on_enter.
            view_dispatcher_send_custom_event(app->view_dispatcher, SavedListEventNoMemory);
            return;
        }
    } else {
        // Back from a file or the options: re-run the query, keep the place.
        radiogeddon_db_view_lock(app->db_view);
        radiogeddon_db_view_unlock(app->db_view, false);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewDb);
}

static void radiogeddon_scene_saved_list_open(RadioGeddonApp* app, uint32_t scene) {
    const RgDbEntry* e =
        radiogeddon_db_at(app->db, radiogeddon_db_view_get_selected(app->db_view));
    if(!e) return;
    radiogeddon_db_path(app->db, e, app->file_path);
    // Opening reads a RAW capture through once to count its samples.
    if(e->kind == RgDbKindRaw && e->size > SAVED_LIST_BUSY_BYTES)
        radiogeddon_scene_show_busy(app, "Opening...");
    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded);
    bool loaded =
        radiogeddon_storage_load(app->storage, furi_string_get_cstr(app->file_path), &app->loaded);
    // A damaged file still opens, with only Details, Rename and Delete.
    app->file_damaged = !loaded || e->kind == RgDbKindCorrupt;
    app->have_loaded_signal = !app->file_damaged;
    app->db_keep = true;
    // A new file: its menu starts at the top.
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
    scene_manager_next_scene(app->scene_manager, scene);
}

bool radiogeddon_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SavedListEventOpen:
            radiogeddon_scene_saved_list_open(app, RadioGeddonSceneSavedInfo);
            consumed = true;
            break;
        case SavedListEventDetails:
            radiogeddon_scene_saved_list_open(app, RadioGeddonSceneFileDetails);
            consumed = true;
            break;
        case SavedListEventSort: {
            RadioGeddonDb* db = radiogeddon_db_view_lock(app->db_view);
            if(db) db->query.sort = (RgDbSort)((db->query.sort + 1) % RgDbSortCount);
            radiogeddon_db_view_unlock(app->db_view, true);
            consumed = true;
            break;
        }
        case SavedListEventOptions:
            app->db_keep = true;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneDbOptions);
            consumed = true;
            break;
        case SavedListEventNoMemory:
            notification_message(app->notifications, &sequence_error);
            popup_reset(app->popup);
            popup_set_header(app->popup, "Not enough memory", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup, "Close other apps\nand try again.", 64, 40, AlignCenter, AlignCenter);
            view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_saved_list_on_exit(void* context) {
    RadioGeddonApp* app = context;
    popup_reset(app->popup);
    if(app->db_keep) return;
    // Leaving the Database: keep its sort order for next time.
    if(app->db && app->db->query.sort != app->settings.db_sort) {
        app->settings.db_sort = (uint8_t)app->db->query.sort;
        radiogeddon_app_save_settings(app);
    }
    radiogeddon_scene_db_release(app);
}
