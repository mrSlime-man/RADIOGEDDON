#include "radiogeddon_scene.h"
#include "../helpers/rg_multi.h"

#if RG_FEATURE_SESSIONS

// Full edition: one research session. Its file is read on entry and written
// (transactionally) after every change; the recordings themselves are only
// ever named, never changed, moved or deleted from here.

typedef enum {
    SessionMenuRecordings,
    SessionMenuAdd,
    SessionMenuRemove,
    SessionMenuActive,
    SessionMenuCompare,
    SessionMenuExport,
    SessionMenuRename,
    SessionMenuDelete,
    SessionMenuEventDeleteYes = 60,
    SessionMenuEventDeleteNo,
    SessionMenuEventFailed,
} SessionMenuItem;

/* Free heap kept beyond a session and its save buffers. */
#define SESSION_HEAP_SPARE  (4u * 1024u)
#define SESSION_REPORT_SIZE (6u * 1024u)

static void radiogeddon_scene_session_menu_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionMenu, index);
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* The session named app->session_name in app->session; false on failure. */
static bool radiogeddon_scene_session_menu_load(RadioGeddonApp* app) {
    if(app->session && strcmp(app->session->name, app->session_name) == 0) return true;
    if(!app->session) {
        if(memmgr_heap_get_max_free_block() <
           sizeof(RgSession) + RADIOGEDDON_SESSION_SCRATCH + SESSION_HEAP_SPARE) {
            app->message_header = "Not enough memory";
            app->message_text = "Close other apps\nand try again.";
            return false;
        }
        app->session = malloc(sizeof(RgSession));
    }
    RgSessionLoad st = radiogeddon_session_load(app->storage, app->session_name, app->session);
    if(st == RgSessionLoadOk || st == RgSessionLoadRecovered) return true;
    app->message_header = "Cannot open";
    app->message_text = "The session file is\nmissing or damaged.";
    free(app->session);
    app->session = NULL;
    return false;
}

static void radiogeddon_scene_session_menu_show(RadioGeddonApp* app) {
    RgSession* s = app->session;
    char active[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(app->storage, active, sizeof(active));
    bool is_active = strcmp(active, s->name) == 0;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, s->name);
    char label[32];
    snprintf(label, sizeof(label), "Recordings (%u)", (unsigned)s->count);
    submenu_add_item(
        submenu, label, SessionMenuRecordings, radiogeddon_scene_session_menu_cb, app);
    if(s->count < RG_SESSION_SIGNALS)
        submenu_add_item(
            submenu, "Add recording...", SessionMenuAdd, radiogeddon_scene_session_menu_cb, app);
    if(s->count)
        submenu_add_item(
            submenu,
            "Remove recording...",
            SessionMenuRemove,
            radiogeddon_scene_session_menu_cb,
            app);
    submenu_add_item(
        submenu,
        is_active ? "Stop collecting" : "Collect new captures",
        SessionMenuActive,
        radiogeddon_scene_session_menu_cb,
        app);
    if(s->count >= 2)
        submenu_add_item(
            submenu,
            "Compare recordings",
            SessionMenuCompare,
            radiogeddon_scene_session_menu_cb,
            app);
    submenu_add_item(
        submenu, "Export report", SessionMenuExport, radiogeddon_scene_session_menu_cb, app);
    submenu_add_item(submenu, "Rename", SessionMenuRename, radiogeddon_scene_session_menu_cb, app);
    submenu_add_item(
        submenu, "Delete session", SessionMenuDelete, radiogeddon_scene_session_menu_cb, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessionMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_session_menu_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(!radiogeddon_scene_session_menu_load(app)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SessionMenuEventFailed);
        return;
    }
    radiogeddon_scene_session_menu_show(app);
}

/* Returns true when a message was shown (it covers this scene). */
static bool radiogeddon_scene_session_menu_add(RadioGeddonApp* app) {
    bool message = true;
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* sel = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);
    if(dialog_file_browser_show(app->dialogs, sel, start, &options)) {
        const char* path = furi_string_get_cstr(sel);
        const char* slash = strrchr(path, '/');
        size_t folder = strlen(RADIOGEDDON_SIGNALS_FOLDER);
        // Sessions name recordings of the signals folder only.
        if(!slash || (size_t)(slash - path) != folder ||
           strncmp(path, RADIOGEDDON_SIGNALS_FOLDER, folder) != 0) {
            radiogeddon_scene_show_message(
                app, "Not added", "Sessions hold files of\nthe signals folder only.");
        } else {
            RgSessionAdd r = rg_session_add(app->session, slash + 1);
            if(r == RgSessionAddOk && !radiogeddon_session_save(app->storage, app->session)) {
                rg_session_remove(app->session, slash + 1);
                radiogeddon_scene_show_message(app, "Not saved", "Check the SD card.");
            } else if(r == RgSessionAddFull) {
                radiogeddon_scene_show_message(
                    app, "Session full", "At most 24 recordings\nin one session.");
            } else if(r == RgSessionAddInvalid) {
                radiogeddon_scene_show_message(
                    app, "Not added", "The file name is too\nlong for a session.");
            } else {
                message = false;
            }
        }
    } else {
        message = false;
    }
    furi_string_free(sel);
    furi_string_free(start);
    return message;
}

static void radiogeddon_scene_session_menu_compare(RadioGeddonApp* app) {
    radiogeddon_multi_clear(app);
    FuriString* path = furi_string_alloc();
    size_t skipped = 0;
    for(size_t i = 0; i < app->session->count; i++) {
        if(!radiogeddon_session_signal_exists(app->storage, app->session->signal[i])) continue;
        furi_string_printf(path, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, app->session->signal[i]);
        if(!radiogeddon_multi_add(app, furi_string_get_cstr(path))) skipped++;
    }
    furi_string_free(path);
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, 1);
    // The compare list opens with the session's recordings (the first 8);
    // the list can still be edited there.
    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMulti);
    if(skipped) {
        radiogeddon_scene_show_message(
            app,
            "First 8 recordings",
            "Compare takes at most\n8; edit the list to\nchoose others.");
    }
}

static void radiogeddon_scene_session_menu_file(void* context, size_t index, size_t count) {
    RadioGeddonApp* app = context;
    snprintf(
        app->multi_header,
        sizeof(app->multi_header),
        "Recording %u of %u",
        (unsigned)(index + 1),
        (unsigned)count);
    radiogeddon_scene_show_progress(app, app->multi_header);
}

/* A text report: the recordings, then the comparison of up to 8 of them by
 * the Multi-Capture Compare module. Written to reports/SESSION_<name>.txt.
 * Returns false with app->message_* set on failure. */
static bool radiogeddon_scene_session_menu_export(RadioGeddonApp* app) {
    RgSession* s = app->session;
    const char* paths[RG_MULTI_MAX_CAPTURES];
    FuriString* path_store[RG_MULTI_MAX_CAPTURES];
    size_t n = 0;
    FuriString* heading = furi_string_alloc();
    char now[20];
    radiogeddon_session_now(now, sizeof(now));
    furi_string_printf(
        heading,
        "RadioGeddon %s session report\nSession: %s\nCreated: %s\nExported: %s\n"
        "Recordings: %u\n================\n",
        RG_EDITION_NAME,
        s->name,
        s->created[0] ? s->created : "-",
        now,
        (unsigned)s->count);
    for(size_t i = 0; i < s->count; i++) {
        bool here = radiogeddon_session_signal_exists(app->storage, s->signal[i]);
        furi_string_cat_printf(
            heading, "%u %s%s\n", (unsigned)(i + 1), s->signal[i], here ? "" : ": missing");
        if(here && n < RG_MULTI_MAX_CAPTURES) {
            path_store[n] =
                furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, s->signal[i]);
            paths[n] = furi_string_get_cstr(path_store[n]);
            n++;
        }
    }
    furi_string_cat_printf(heading, "================\nComparison of %u\n", (unsigned)n);

    bool ok = radiogeddon_scene_module_load(
        app,
        RADIOGEDDON_MODULE_MULTI,
        radiogeddon_scene_multi_memory(n) + SESSION_REPORT_SIZE + SESSION_HEAP_SPARE);
    char* buf = NULL;
    size_t len = 0;
    if(ok) {
        const RadioGeddonMultiModule* api = app->module_api;
        buf = malloc(SESSION_REPORT_SIZE);
        len = api->compare(
            app->storage,
            paths,
            n,
            furi_string_get_cstr(heading),
            buf,
            SESSION_REPORT_SIZE,
            radiogeddon_scene_session_menu_file,
            radiogeddon_scene_progress,
            app);
        radiogeddon_scene_progress_end(app);
        radiogeddon_scene_module_unload(app);
    }
    for(size_t i = 0; i < n; i++)
        furi_string_free(path_store[i]);
    furi_string_free(heading);

    if(ok) {
        storage_common_mkdir(app->storage, RADIOGEDDON_REPORTS_FOLDER);
        char name[RG_SESSION_NAME_MAX + 8];
        snprintf(name, sizeof(name), "SESSION_%s", s->name);
        for(char* c = name; *c; c++)
            if(*c == ' ') *c = '_';
        FuriString* where = furi_string_alloc();
        ok = radiogeddon_storage_make_unique_path_in(
            app->storage, where, RADIOGEDDON_REPORTS_FOLDER, name, ".txt");
        if(ok) {
            File* file = storage_file_alloc(app->storage);
            ok = storage_file_open(
                     file, furi_string_get_cstr(where), FSAM_WRITE, FSOM_CREATE_NEW) &&
                 storage_file_write(file, buf, len) == len;
            ok = storage_file_close(file) && ok;
            storage_file_free(file);
            if(!ok) storage_common_remove(app->storage, furi_string_get_cstr(where));
        }
        furi_string_free(where);
        if(!ok) {
            app->message_header = "Export failed";
            app->message_text = "The SD card refused\nthe report file.";
        }
    }
    free(buf);
    return ok;
}

bool radiogeddon_scene_session_menu_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        // Back while confirming a delete returns to the menu.
        if(scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessionMenu) ==
           SessionMenuEventDeleteYes) {
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSessionMenu, SessionMenuDelete);
            radiogeddon_scene_session_menu_show(app);
            return true;
        }
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SessionMenuEventFailed) {
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    if(!app->session) return false;
    switch(event.event) {
    case SessionMenuRecordings:
    case SessionMenuRemove:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneSessionSignals, event.event == SessionMenuRemove);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessionSignals);
        break;
    case SessionMenuAdd:
        if(!radiogeddon_scene_session_menu_add(app)) radiogeddon_scene_session_menu_show(app);
        break;
    case SessionMenuActive: {
        char active[RG_SESSION_NAME_MAX];
        radiogeddon_session_active(app->storage, active, sizeof(active));
        bool is_active = strcmp(active, app->session->name) == 0;
        if(!radiogeddon_session_set_active(app->storage, is_active ? "" : app->session->name)) {
            radiogeddon_scene_show_message(app, "Not saved", "Check the SD card.");
        } else {
            notification_message(app->notifications, &sequence_blink_blue_10);
            radiogeddon_scene_session_menu_show(app);
        }
        break;
    }
    case SessionMenuCompare:
        radiogeddon_scene_session_menu_compare(app);
        break;
    case SessionMenuExport:
        if(radiogeddon_scene_session_menu_export(app)) {
            notification_message(app->notifications, &sequence_success);
            radiogeddon_scene_show_message(
                app, "Report saved", "In apps_data/\nradiogeddon/reports");
        } else {
            radiogeddon_scene_show_message(app, app->message_header, app->message_text);
        }
        break;
    case SessionMenuRename:
        app->session_name_mode = RadioGeddonSessionNameRename;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessionName);
        break;
    case SessionMenuDelete:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneSessionMenu, SessionMenuEventDeleteYes);
        snprintf(
            app->tx_text,
            sizeof(app->tx_text),
            "%s\nIts %u recordings stay\nin the Database.",
            app->session->name,
            (unsigned)app->session->count);
        radiogeddon_scene_show_confirm(
            app,
            "Delete session?",
            app->tx_text,
            "Delete",
            SessionMenuEventDeleteYes,
            SessionMenuEventDeleteNo);
        break;
    case SessionMenuEventDeleteYes:
        radiogeddon_session_delete(app->storage, app->session->name);
        free(app->session);
        app->session = NULL;
        notification_message(app->notifications, &sequence_success);
        scene_manager_previous_scene(app->scene_manager);
        break;
    case SessionMenuEventDeleteNo:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneSessionMenu, SessionMenuDelete);
        radiogeddon_scene_session_menu_show(app);
        break;
    default:
        return false;
    }
    return true;
}

void radiogeddon_scene_session_menu_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}

#endif
