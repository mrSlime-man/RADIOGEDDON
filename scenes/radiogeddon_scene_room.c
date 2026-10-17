#include "radiogeddon_scene.h"

#include <toolbox/version.h>

#if RG_FEATURE_DOLPHIN_LAB

/*
 * Full edition: the Dolphin Lab room 2.0, in the lab module with its
 * screen (loaded by the app's forwarder while the room is shown). The radio
 * is never used here; the screen is freed before any tool the room opens
 * starts (on_exit runs first). The
 * workbench and the oscilloscope show the recording in context, the
 * terminal the Research Journal's counts (nothing is listed from the card).
 */

typedef enum {
    RoomEventActionBase = 1110, // + station
} RoomEvent;

static uint8_t room_gallery = RgMascotStateIdle;
static uint32_t room_gallery_since;

static RG_UI_TEXT void radiogeddon_scene_room_cb(uint8_t station, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RoomEventActionBase + station);
}

/* The oscilloscope: the first samples of a RAW recording the user picks. */
static RG_UI_TEXT void radiogeddon_scene_room_scope(RadioGeddonApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* sel = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);
    if(dialog_file_browser_show(app->dialogs, sel, start, &options)) {
        radiogeddon_room_load_scope(app->room_view, app->storage, furi_string_get_cstr(sel));
    }
    furi_string_free(start);
    furi_string_free(sel);
}

RG_UI_TEXT void radiogeddon_scene_room_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->room_view = radiogeddon_room_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewRoom, radiogeddon_room_view_get_view(app->room_view));
    radiogeddon_room_view_set_callback(app->room_view, radiogeddon_scene_room_cb, app);
    radiogeddon_room_load_terminal(
        app->room_view,
        &app->research.journal,
        app->settings.research_xp,
        version_get_version(NULL));
    // The workbench and the oscilloscope: the recording in context.
    char session[RG_SESSION_NAME_MAX] = "";
#if RG_FEATURE_SESSIONS
    radiogeddon_session_active(app->storage, session, sizeof(session));
#endif
    FuriString* ctx = furi_string_alloc();
    bool have = radiogeddon_research_context_path(app, ctx);
    radiogeddon_room_view_set_workbench(
        app->room_view, have ? app->research.ctx_name : "", session);
    if(have && app->research.ctx_raw)
        radiogeddon_room_load_scope(app->room_view, app->storage, furi_string_get_cstr(ctx));
    furi_string_free(ctx);
    uint8_t level = rg_journal_unlock_level(&app->research.journal, app->settings.research_xp);
    radiogeddon_room_view_set_decoration(
        app->room_view,
        rg_decoration_unlocked((RgDecoration)app->settings.decoration, level) ?
            app->settings.decoration :
            0);
    room_gallery_since = furi_get_tick();
    radiogeddon_memdiag_sample("Dolphin Lab room");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewRoom);
}

/* The dolphin's pose at each station (the corner shows the chosen one),
 * with the look and animation level chosen. */
static RG_UI_TEXT void radiogeddon_scene_room_frame(RadioGeddonApp* app, RgMascotFrame* f) {
    uint32_t now = furi_get_tick();
    uint8_t station = radiogeddon_room_view_get_station(app->room_view);
    uint32_t t = now - room_gallery_since;
    RgMascotLook look;
    memcpy(&look, app->settings.look, sizeof(look));
    switch(station) {
    case RadioGeddonRoomCorner: {
        uint8_t g = radiogeddon_room_view_get_gallery(app->room_view);
        if(g != room_gallery) {
            room_gallery = g;
            room_gallery_since = now;
            t = 0;
        }
        rg_mascot_pose((RgMascotState)g, t, app->ui.mascot.seed, 60, false, f);
        rg_mascot_style(f, &look, app->ui.mascot.anim, t);
        // A demonstration: no REC lamp when nothing records.
        if(f->fx == RgMascotFxRec) f->fx = RgMascotFxNone;
        return;
    }
    case RadioGeddonRoomRadar:
        rg_mascot_pose(RgMascotStateScanning, t, 0, RG_MASCOT_PROGRESS_NONE, false, f);
        break;
    case RadioGeddonRoomRack:
        rg_mascot_pose(
            RgMascotStateIdle, t, app->ui.mascot.seed, RG_MASCOT_PROGRESS_NONE, false, f);
        f->fin = RgMascotFinPoint;
        f->eye_dx = 1;
        break;
    default:
        rg_mascot_pose(
            RgMascotStateIdle, t, app->ui.mascot.seed, RG_MASCOT_PROGRESS_NONE, false, f);
        f->eye_dx = 1;
        break;
    }
    rg_mascot_style(f, &look, app->ui.mascot.anim, t);
    // The station draws its own instrument beside the dolphin.
    f->prop = RgMascotPropNone;
}

/* Open the recording in context's file menu (or the Database). */
static RG_UI_TEXT void radiogeddon_scene_room_workbench(RadioGeddonApp* app) {
    if(radiogeddon_research_context_path(app, app->file_path)) {
        radiogeddon_loaded_signal_reset(&app->loaded);
        radiogeddon_loaded_signal_init(&app->loaded);
        bool ok = radiogeddon_storage_load(
            app->storage, furi_string_get_cstr(app->file_path), &app->loaded);
        app->file_damaged = !ok;
        app->have_loaded_signal = ok;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
    } else {
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedList);
    }
}

RG_UI_TEXT bool radiogeddon_scene_room_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(!app->room_view) return false;
    if(event.type == SceneManagerEventTypeTick) {
        RgMascotFrame f;
        radiogeddon_scene_room_frame(app, &f);
        radiogeddon_room_view_update(app->room_view, &f, furi_get_tick());
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom || event.event < RoomEventActionBase ||
       event.event >= RoomEventActionBase + RadioGeddonRoomCount)
        return false;
    radiogeddon_ui_event(app, RgMascotEventUserInput, 0);
    switch(event.event - RoomEventActionBase) {
    case RadioGeddonRoomWorkbench:
        radiogeddon_scene_room_workbench(app);
        break;
    case RadioGeddonRoomScope:
        radiogeddon_scene_room_scope(app);
        break;
    case RadioGeddonRoomRadar:
        app->scan_mode_run = RgAdaptStandard;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        break;
    case RadioGeddonRoomTerminal:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneJournal);
        break;
    case RadioGeddonRoomRack:
        switch(radiogeddon_room_view_get_rack(app->room_view)) {
        case RadioGeddonRackScanner:
            app->scan_mode_run = RgAdaptStandard;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
            break;
        case RadioGeddonRackAdaptive:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAdaptive);
            break;
        case RadioGeddonRackLogger:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneActlog);
            break;
        default:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            break;
        }
        break;
    case RadioGeddonRoomCorner:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCustomize);
        break;
    default:
        break;
    }
    return true;
}

RG_UI_TEXT void radiogeddon_scene_room_on_exit(void* context) {
    RadioGeddonApp* app = context;
    if(app->room_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewRoom);
        radiogeddon_room_view_free(app->room_view);
        app->room_view = NULL;
    }
}

#endif
