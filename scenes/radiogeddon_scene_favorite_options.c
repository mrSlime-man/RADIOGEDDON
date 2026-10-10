#include "radiogeddon_scene.h"

#if RG_FEATURE_FAVORITES

// Full edition: what to do with one favorite.

typedef enum {
    FavoriteOptReceive,
    FavoriteOptRecord,
    FavoriteOptSetDefault,
    FavoriteOptDelete,
    FavoriteOptDeleteYes = 990,
    FavoriteOptDeleteNo,
} FavoriteOpt;

static void radiogeddon_scene_favorite_options_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void radiogeddon_scene_favorite_options_menu(RadioGeddonApp* app) {
    RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(fav->freq[app->favorite_index], freq, sizeof(freq));
    furi_string_printf(app->temp_str, "%s MHz", freq);
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, furi_string_get_cstr(app->temp_str));
    submenu_add_item(
        submenu, "Receive here", FavoriteOptReceive, radiogeddon_scene_favorite_options_cb, app);
    submenu_add_item(
        submenu, "Receive + record", FavoriteOptRecord, radiogeddon_scene_favorite_options_cb, app);
    submenu_add_item(
        submenu,
        "Use as receive freq",
        FavoriteOptSetDefault,
        radiogeddon_scene_favorite_options_cb,
        app);
    submenu_add_item(
        submenu, "Delete", FavoriteOptDelete, radiogeddon_scene_favorite_options_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_favorite_options_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(app->favorite_index >= radiogeddon_scene_favorites(app)->count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    radiogeddon_scene_favorite_options_menu(app);
}

bool radiogeddon_scene_favorite_options_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
    if(app->favorite_index >= fav->count) return false;
    uint32_t hz = fav->freq[app->favorite_index];

    switch(event.event) {
    case FavoriteOptReceive:
    case FavoriteOptRecord:
    case FavoriteOptSetDefault:
        if(!radiogeddon_subghz_is_frequency_allowed(app->subghz, hz)) {
            radiogeddon_scene_show_message(
                app, "Cannot tune there", "The radio in use cannot\ntune to that frequency.");
            return true;
        }
        app->frequency = hz;
        radiogeddon_subghz_set_frequency(app->subghz, hz);
        if(event.event == FavoriteOptSetDefault) {
            radiogeddon_app_save_settings(app);
            notification_message(app->notifications, &sequence_success);
            scene_manager_previous_scene(app->scene_manager);
        } else {
            app->receiver_autorecord = (event.event == FavoriteOptRecord);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        }
        return true;
    case FavoriteOptDelete: {
        char freq[RG_FREQ_TEXT_SIZE];
        rg_freq_text(hz, freq, sizeof(freq));
        furi_string_printf(app->temp_str, "%s MHz", freq);
        radiogeddon_scene_show_confirm(
            app,
            "Delete favorite?",
            furi_string_get_cstr(app->temp_str),
            "Delete",
            FavoriteOptDeleteYes,
            FavoriteOptDeleteNo);
        return true;
    }
    case FavoriteOptDeleteYes:
        radiogeddon_favorites_remove(fav, app->favorite_index);
        if(!radiogeddon_favorites_save(app->storage, fav)) {
            // Keep the list on screen in step with the file: read it again.
            app->favorites_loaded = false;
            radiogeddon_scene_show_message(
                app, "Save failed", "Could not write the\nfavorites file.");
            return true;
        }
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case FavoriteOptDeleteNo:
        radiogeddon_scene_favorite_options_menu(app);
        return true;
    default:
        return false;
    }
}

void radiogeddon_scene_favorite_options_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}

#endif
