#include "../radiogeddon.h"
#include <toolbox/path.h>

typedef enum {
    RadioGeddonReplayStateConfirm = 0,
    RadioGeddonReplayStateTx = 1,
} RadioGeddonReplayState;

typedef enum {
    RadioGeddonReplayEventSend = 400,
    RadioGeddonReplayEventCancel = 401,
    RadioGeddonReplayEventPopupDone = 402,
} RadioGeddonReplayEvent;

static void
    radiogeddon_replay_button_callback(GuiButtonType result, InputType type, void* context) {
    RadioGeddon* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonReplayEventSend);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonReplayEventCancel);
    }
}

static void radiogeddon_replay_popup_callback(void* context) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonReplayEventPopupDone);
}

static void radiogeddon_replay_show_confirm(RadioGeddon* app) {
    FuriString* name = furi_string_alloc();
    FuriString* info = furi_string_alloc();
    path_extract_filename(app->file_path, name, true);
    rg_storage_read_info(furi_string_get_cstr(app->file_path), info);

    FuriString* body = furi_string_alloc();
    furi_string_printf(
        body,
        "%s\n%s\n\n! Transmits on air !\nUse only on signals you\nare authorized to send.",
        furi_string_get_cstr(name),
        furi_string_get_cstr(info));

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 40, furi_string_get_cstr(body));
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", radiogeddon_replay_button_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Send", radiogeddon_replay_button_callback, app);

    furi_string_free(name);
    furi_string_free(info);
    furi_string_free(body);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

void radiogeddon_scene_replay_on_enter(void* context) {
    RadioGeddon* app = context;

    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneReplay, RadioGeddonReplayStateConfirm);

    /* If entered directly from the menu, prompt for a file first. */
    if(furi_string_empty(app->file_path)) {
        FuriString* start_path = furi_string_alloc_set(RG_SUBGHZ_FOLDER);
        DialogsFileBrowserOptions options;
        dialog_file_browser_set_basic_options(&options, RG_SUBGHZ_EXTENSION, NULL);
        options.base_path = RG_SUBGHZ_FOLDER;
        options.hide_ext = true;
        options.skip_assets = true;
        bool chosen = dialog_file_browser_show(app->dialogs, app->file_path, start_path, &options);
        furi_string_free(start_path);
        if(!chosen) {
            scene_manager_previous_scene(app->scene_manager);
            return;
        }
    }

    radiogeddon_replay_show_confirm(app);
}

bool radiogeddon_scene_replay_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case RadioGeddonReplayEventCancel:
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        case RadioGeddonReplayEventSend: {
            FuriString* error = furi_string_alloc();
            bool ok = rg_radio_begin(app->radio);
            if(ok) {
                ok = rg_radio_replay_file_start(
                    app->radio, furi_string_get_cstr(app->file_path), error);
            } else {
                furi_string_set(error, "Radio not available");
            }

            if(ok) {
                scene_manager_set_scene_state(
                    app->scene_manager, RadioGeddonSceneReplay, RadioGeddonReplayStateTx);
                radiogeddon_notify(app, &sequence_blink_start_magenta);
                widget_reset(app->widget);
                widget_add_string_element(
                    app->widget, 64, 28, AlignCenter, AlignCenter, FontPrimary, "Transmitting...");
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
            } else {
                radiogeddon_notify(app, &sequence_error);
                popup_reset(app->popup);
                popup_set_header(app->popup, "Cannot replay", 64, 12, AlignCenter, AlignTop);
                popup_set_text(
                    app->popup, furi_string_get_cstr(error), 64, 36, AlignCenter, AlignCenter);
                popup_set_callback(app->popup, radiogeddon_replay_popup_callback);
                popup_set_context(app->popup, app);
                popup_set_timeout(app->popup, 2000);
                popup_enable_timeout(app->popup);
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            }
            furi_string_free(error);
            consumed = true;
            break;
        }
        case RadioGeddonReplayEventPopupDone:
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        uint32_t state = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneReplay);
        if(state == RadioGeddonReplayStateTx) {
            if(!rg_radio_tx_is_running(app->radio)) {
                rg_radio_stop_tx(app->radio);
                radiogeddon_notify(app, &sequence_blink_stop);
                radiogeddon_notify(app, &sequence_success);
                scene_manager_set_scene_state(
                    app->scene_manager, RadioGeddonSceneReplay, RadioGeddonReplayStateConfirm);

                popup_reset(app->popup);
                popup_set_header(app->popup, "Sent", 64, 20, AlignCenter, AlignCenter);
                popup_set_callback(app->popup, radiogeddon_replay_popup_callback);
                popup_set_context(app->popup, app);
                popup_set_timeout(app->popup, 1200);
                popup_enable_timeout(app->popup);
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            }
        }
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_replay_on_exit(void* context) {
    RadioGeddon* app = context;
    /* Always make sure TX is stopped when leaving, however we got here. */
    if(rg_radio_get_state(app->radio) == RgRadioStateTx) {
        rg_radio_stop_tx(app->radio);
        radiogeddon_notify(app, &sequence_blink_stop);
    }
    widget_reset(app->widget);
    popup_reset(app->popup);
}
