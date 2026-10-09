#include "../radiogeddon.h"
#include <toolbox/path.h>

typedef enum {
    RadioGeddonInfoEventDelete = 300,
    RadioGeddonInfoEventReplay = 301,
    RadioGeddonInfoEventCompare = 302,
    RadioGeddonInfoEventPopupDone = 303,
} RadioGeddonInfoEvent;

static void radiogeddon_info_button_callback(GuiButtonType result, InputType type, void* context) {
    RadioGeddon* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonInfoEventDelete);
    } else if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonInfoEventReplay);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonInfoEventCompare);
    }
}

static void radiogeddon_info_popup_callback(void* context) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonInfoEventPopupDone);
}

void radiogeddon_scene_saved_info_on_enter(void* context) {
    RadioGeddon* app = context;

    FuriString* name = furi_string_alloc();
    FuriString* info = furi_string_alloc();
    path_extract_filename(app->file_path, name, true);

    if(!rg_storage_read_info(furi_string_get_cstr(app->file_path), info)) {
        /* read_info already set an error message into info */
    }

    FuriString* body = furi_string_alloc();
    furi_string_printf(body, "%s\n%s", furi_string_get_cstr(name), furi_string_get_cstr(info));

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 40, furi_string_get_cstr(body));
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Delete", radiogeddon_info_button_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Replay", radiogeddon_info_button_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Compare", radiogeddon_info_button_callback, app);

    furi_string_free(name);
    furi_string_free(info);
    furi_string_free(body);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

bool radiogeddon_scene_saved_info_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case RadioGeddonInfoEventReplay:
            /* app->file_path already points at the selected file. */
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReplay);
            consumed = true;
            break;
        case RadioGeddonInfoEventCompare:
            /* Seed comparison slot A, then let the Compare scene pick B. */
            furi_string_set(app->compare_a, app->file_path);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCompare);
            consumed = true;
            break;
        case RadioGeddonInfoEventDelete:
            if(rg_storage_delete(furi_string_get_cstr(app->file_path))) {
                radiogeddon_notify(app, &sequence_success);
                popup_reset(app->popup);
                popup_set_header(app->popup, "Deleted", 64, 20, AlignCenter, AlignCenter);
            } else {
                radiogeddon_notify(app, &sequence_error);
                popup_reset(app->popup);
                popup_set_header(app->popup, "Delete failed", 64, 20, AlignCenter, AlignCenter);
            }
            popup_set_callback(app->popup, radiogeddon_info_popup_callback);
            popup_set_context(app->popup, app);
            popup_set_timeout(app->popup, 1200);
            popup_enable_timeout(app->popup);
            view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            consumed = true;
            break;
        case RadioGeddonInfoEventPopupDone:
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, RadioGeddonSceneStart);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_saved_info_on_exit(void* context) {
    RadioGeddon* app = context;
    widget_reset(app->widget);
    popup_reset(app->popup);
}
