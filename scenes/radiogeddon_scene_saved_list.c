#include "radiogeddon_scene.h"

// Signal Database list. The index is built when the list opens and kept while
// a file or the options opened from it are shown; it is rebuilt after files
// change (db_dirty) and freed on returning to the main menu.

typedef enum {
    SavedListEventOpen = 400,
    SavedListEventSort,
    SavedListEventOptions,
    SavedListEventNoMemory,
} SavedListEvent;

static void radiogeddon_scene_saved_list_view_cb(RadioGeddonDbViewEvent event, void* context) {
    RadioGeddonApp* app = context;
    uint32_t out = event == RadioGeddonDbViewEventOpen ? SavedListEventOpen :
                   event == RadioGeddonDbViewEventSort ? SavedListEventSort :
                                                         SavedListEventOptions;
    view_dispatcher_send_custom_event(app->view_dispatcher, out);
}

/* (Re)build the index, keeping the query and the highlighted position. */
static bool radiogeddon_scene_saved_list_load(RadioGeddonApp* app) {
    RgDbQuery query = {.sort = RgDbSortDate, .show = RgDbShowAll};
    size_t selected = 0;
    if(app->db) {
        query = app->db->query;
        selected = radiogeddon_db_view_get_selected(app->db_view);
    }
    radiogeddon_scene_show_busy(app, "Loading...");
    // Detach before freeing so the list never draws a freed index.
    radiogeddon_db_view_set_db(app->db_view, NULL);
    radiogeddon_db_free(app->db);
    app->db = NULL;
    app->db_dirty = false;

    RadioGeddonDbStatus status;
    app->db = radiogeddon_db_load(app->storage, &status);
    if(!app->db) return false;
    app->db->query = query;
    radiogeddon_db_apply(app->db);
    radiogeddon_db_view_set_db(app->db_view, app->db);
    radiogeddon_db_view_set_selected(app->db_view, selected);
    return true;
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

static void radiogeddon_scene_saved_list_open(RadioGeddonApp* app) {
    const RgDbEntry* e =
        radiogeddon_db_at(app->db, radiogeddon_db_view_get_selected(app->db_view));
    if(!e) return;
    FuriString* path = furi_string_alloc();
    radiogeddon_db_path(app->db, e, path);
    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded);
    if(radiogeddon_storage_load(app->storage, furi_string_get_cstr(path), &app->loaded)) {
        furi_string_set(app->file_path, path);
        app->have_loaded_signal = true;
        app->db_keep = true;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
    } else {
        notification_message(app->notifications, &sequence_error);
    }
    furi_string_free(path);
}

bool radiogeddon_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SavedListEventOpen:
            radiogeddon_scene_saved_list_open(app);
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
    if(!app->db_keep) radiogeddon_scene_db_release(app);
}
