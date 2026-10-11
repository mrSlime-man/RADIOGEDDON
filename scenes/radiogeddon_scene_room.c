#include "radiogeddon_scene.h"

#include <toolbox/version.h>

#if RG_FEATURE_DOLPHIN_LAB

/*
 * Full edition: the Dolphin Lab room. Its screen is a module loaded while
 * the room is open; the radio is never used here, and the module is
 * unloaded before any tool the room opens starts (on_exit runs first).
 */

#define ROOM_API(app) ((const RadioGeddonLabModule*)(app)->module_api)

/* The room's work besides its module: its screen, the trace's samples and
 * their reader. */
#define ROOM_WORK (1024u + 512u * sizeof(int32_t) + sizeof(RgRawReader))

typedef enum {
    RoomEventFailed = 1100,
    RoomEventActionBase = 1110, // + station
} RoomEvent;

static uint8_t room_gallery = RgMascotStateIdle;
static uint32_t room_gallery_since;

static void radiogeddon_scene_room_cb(uint8_t station, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RoomEventActionBase + station);
}

/* The oscilloscope: the first samples of a RAW recording the user picks. */
static void radiogeddon_scene_room_scope(RadioGeddonApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* sel = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);
    if(dialog_file_browser_show(app->dialogs, sel, start, &options)) {
        ROOM_API(app)->load_scope(app->room_view, app->storage, furi_string_get_cstr(sel));
    }
    furi_string_free(start);
    furi_string_free(sel);
}

void radiogeddon_scene_room_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(!radiogeddon_scene_module_load(app, RADIOGEDDON_MODULE_LAB, ROOM_WORK)) {
        // Leave from the event loop, then say why (as other tools do).
        view_dispatcher_send_custom_event(app->view_dispatcher, RoomEventFailed);
        return;
    }
    const RadioGeddonLabModule* api = ROOM_API(app);
    app->room_view = api->alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewRoom, api->get_view(app->room_view));
    api->set_callback(app->room_view, radiogeddon_scene_room_cb, app);
    api->load_terminal(app->room_view, app->storage, version_get_version(NULL));
    room_gallery_since = furi_get_tick();
    radiogeddon_memdiag_sample("Dolphin Lab room");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewRoom);
}

/* The dolphin's pose at each station (the gallery shows the chosen one). */
static void radiogeddon_scene_room_frame(RadioGeddonApp* app, RgMascotFrame* f) {
    uint32_t now = furi_get_tick();
    const RadioGeddonLabModule* api = ROOM_API(app);
    uint8_t station = api->get_station(app->room_view);
    uint32_t t = now - room_gallery_since;
    switch(station) {
    case RadioGeddonRoomGallery: {
        uint8_t g = api->get_gallery(app->room_view);
        if(g != room_gallery) {
            room_gallery = g;
            room_gallery_since = now;
            t = 0;
        }
        rg_mascot_pose((RgMascotState)g, t, app->ui.mascot.seed, 60, false, f);
        // A demonstration: no REC lamp when nothing records.
        if(f->fx == RgMascotFxRec) f->fx = RgMascotFxNone;
        return;
    }
    case RadioGeddonRoomRadar:
        rg_mascot_pose(RgMascotStateScanning, t, 0, RG_MASCOT_PROGRESS_NONE, false, f);
        break;
    case RadioGeddonRoomAntenna:
        rg_mascot_pose(RgMascotStateReceiving, t, 0, RG_MASCOT_PROGRESS_NONE, false, f);
        break;
    default:
        rg_mascot_pose(
            RgMascotStateIdle, t, app->ui.mascot.seed, RG_MASCOT_PROGRESS_NONE, false, f);
        f->eye_dx = 1;
        break;
    }
    // The station draws its own instrument beside the dolphin.
    f->prop = RgMascotPropNone;
}

bool radiogeddon_scene_room_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(!app->room_view) {
        if(event.type == SceneManagerEventTypeCustom && event.event == RoomEventFailed) {
            const char* header = app->message_header;
            const char* text = app->message_text;
            scene_manager_previous_scene(app->scene_manager);
            radiogeddon_scene_show_message(app, header, text);
            return true;
        }
        return false;
    }
    if(event.type == SceneManagerEventTypeTick) {
        RgMascotFrame f;
        radiogeddon_scene_room_frame(app, &f);
        ROOM_API(app)->update(app->room_view, &f, furi_get_tick());
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom || event.event < RoomEventActionBase ||
       event.event >= RoomEventActionBase + RadioGeddonRoomCount)
        return false;
    radiogeddon_ui_event(app, RgMascotEventUserInput, 0);
    switch(event.event - RoomEventActionBase) {
    case RadioGeddonRoomRadar:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        break;
    case RadioGeddonRoomScope:
        radiogeddon_scene_room_scope(app);
        break;
    case RadioGeddonRoomAntenna:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        break;
    case RadioGeddonRoomRecorder:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedList);
        break;
    case RadioGeddonRoomTerminal:
        ROOM_API(app)->load_terminal(app->room_view, app->storage, version_get_version(NULL));
        break;
    default:
        break;
    }
    return true;
}

void radiogeddon_scene_room_on_exit(void* context) {
    RadioGeddonApp* app = context;
    if(app->room_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewRoom);
        ROOM_API(app)->free(app->room_view);
        app->room_view = NULL;
    }
    if(app->module) radiogeddon_scene_module_unload(app);
}

#endif
