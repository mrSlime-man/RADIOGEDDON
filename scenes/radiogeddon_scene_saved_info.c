#include "radiogeddon_scene.h"

typedef enum {
    SavedInfoIndexAnalyze,
    SavedInfoIndexUnknown,
    SavedInfoIndexTimeline,
    SavedInfoIndexCrypto,
    SavedInfoIndexCompare,
    SavedInfoIndexReplay,
    SavedInfoIndexDelete,
} SavedInfoIndex;

/* Custom events for the delete confirmation (offset clear of menu indices). */
typedef enum {
    SavedInfoEventDeleteConfirm = 1000,
    SavedInfoEventDeleteCancel,
} SavedInfoEvent;

/* Scene state: which view we are showing. */
typedef enum {
    SavedInfoStateMenu = 0,
    SavedInfoStateConfirmDelete = 1,
} SavedInfoState;

static void radiogeddon_scene_saved_info_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void radiogeddon_scene_saved_info_delete_button_cb(
    GuiButtonType result,
    InputType type,
    void* context) {
    RadioGeddonApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SavedInfoEventDeleteCancel);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SavedInfoEventDeleteConfirm);
    }
}

static void radiogeddon_scene_saved_info_show_menu(RadioGeddonApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, furi_string_get_cstr(app->loaded.name));
    submenu_add_item(
        submenu,
        "Signal Info & Analysis",
        SavedInfoIndexAnalyze,
        radiogeddon_scene_saved_info_cb,
        app);
    submenu_add_item(
        submenu,
        "Unknown Protocol Analysis",
        SavedInfoIndexUnknown,
        radiogeddon_scene_saved_info_cb,
        app);
    if(app->loaded.kind == RadioGeddonSignalKindRaw) {
        submenu_add_item(
            submenu,
            "Pulse Timeline",
            SavedInfoIndexTimeline,
            radiogeddon_scene_saved_info_cb,
            app);
    }
    submenu_add_item(
        submenu, "Crypto Analysis", SavedInfoIndexCrypto, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Compare with...", SavedInfoIndexCompare, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Replay (TX)", SavedInfoIndexReplay, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Delete", SavedInfoIndexDelete, radiogeddon_scene_saved_info_cb, app);
    submenu_set_selected_item(
        submenu,
        scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo) ==
                SavedInfoStateConfirmDelete ?
            SavedInfoIndexDelete :
            0);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

static void radiogeddon_scene_saved_info_show_confirm(RadioGeddonApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_string_element(
        widget, 64, 4, AlignCenter, AlignTop, FontPrimary, "Delete recording?");
    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str, "%s\nThis cannot be undone.", furi_string_get_cstr(app->loaded.name));
    widget_add_text_box_element(
        widget,
        0,
        20,
        128,
        28,
        AlignCenter,
        AlignCenter,
        furi_string_get_cstr(app->temp_str),
        false);
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Cancel", radiogeddon_scene_saved_info_delete_button_cb, app);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Delete", radiogeddon_scene_saved_info_delete_button_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

void radiogeddon_scene_saved_info_on_enter(void* context) {
    RadioGeddonApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneSavedInfo, SavedInfoStateMenu);
    radiogeddon_scene_saved_info_show_menu(app);
}

bool radiogeddon_scene_saved_info_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SavedInfoIndexAnalyze:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAnalyze);
            consumed = true;
            break;
        case SavedInfoIndexUnknown:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneUnknown);
            consumed = true;
            break;
        case SavedInfoIndexTimeline:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneTimeline);
            consumed = true;
            break;
        case SavedInfoIndexCrypto:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCrypto);
            consumed = true;
            break;
        case SavedInfoIndexCompare: {
            DialogsFileBrowserOptions options;
            dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
            options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
            FuriString* sel = furi_string_alloc();
            FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);
            bool chosen = dialog_file_browser_show(app->dialogs, sel, start, &options);
            if(chosen) {
                radiogeddon_loaded_signal_reset(&app->loaded_b);
                radiogeddon_loaded_signal_init(&app->loaded_b);
                if(radiogeddon_storage_load(
                       app->storage, furi_string_get_cstr(sel), &app->loaded_b)) {
                    furi_string_set(app->file_path_b, sel);
                    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCompareResult);
                } else {
                    notification_message(app->notifications, &sequence_error);
                }
            }
            furi_string_free(sel);
            furi_string_free(start);
            consumed = true;
            break;
        }
        case SavedInfoIndexReplay:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReplay);
            consumed = true;
            break;
        case SavedInfoIndexDelete:
            // Show a confirmation instead of deleting immediately.
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSavedInfo, SavedInfoStateConfirmDelete);
            radiogeddon_scene_saved_info_show_confirm(app);
            consumed = true;
            break;
        case SavedInfoEventDeleteConfirm:
            storage_simply_remove(app->storage, furi_string_get_cstr(app->file_path));
            notification_message(app->notifications, &sequence_success);
            app->have_loaded_signal = false;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        case SavedInfoEventDeleteCancel:
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSavedInfo, SavedInfoStateMenu);
            radiogeddon_scene_saved_info_show_menu(app);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Back while confirming returns to the menu rather than leaving.
        if(scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo) ==
           SavedInfoStateConfirmDelete) {
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSavedInfo, SavedInfoStateMenu);
            radiogeddon_scene_saved_info_show_menu(app);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_saved_info_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
