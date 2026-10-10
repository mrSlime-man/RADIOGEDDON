#include "radiogeddon_scene.h"

#if RG_FEATURE_FAVORITES

// Full edition: favorite frequencies. The first rows add the current receive
// frequency or a typed one; each favorite opens its options (receive there,
// make it the receive frequency, delete). Settings > Scan / Hop source can use
// the whole list.

typedef enum {
    FavoritesIndexAddCurrent = 0,
    FavoritesIndexAddTyped,
    FavoritesIndexFirst, // + favorite index
} FavoritesIndex;

static void radiogeddon_scene_favorites_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_favorites_on_enter(void* context) {
    RadioGeddonApp* app = context;
    RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    furi_string_printf(
        app->temp_str,
        "Favorites %u/%u",
        (unsigned)fav->count,
        (unsigned)RADIOGEDDON_FAVORITES_MAX);
    submenu_set_header(submenu, furi_string_get_cstr(app->temp_str));

    char freq[RG_FREQ_TEXT_SIZE];
    char label[32];
    rg_freq_text(app->frequency, freq, sizeof(freq));
    snprintf(label, sizeof(label), "+ Add %s MHz", freq);
    submenu_add_item(
        submenu, label, FavoritesIndexAddCurrent, radiogeddon_scene_favorites_cb, app);
    submenu_add_item(
        submenu, "+ Type a frequency", FavoritesIndexAddTyped, radiogeddon_scene_favorites_cb, app);
    for(size_t i = 0; i < fav->count; i++) {
        rg_freq_text(fav->freq[i], freq, sizeof(freq));
        bool tunable = radiogeddon_subghz_is_frequency_allowed(app->subghz, fav->freq[i]);
        // A favorite the radio in use cannot tune (another radio or firmware
        // saved it) is kept but marked.
        snprintf(label, sizeof(label), "%s MHz%s", freq, tunable ? "" : " (no tune)");
        submenu_add_item(
            submenu, label, FavoritesIndexFirst + i, radiogeddon_scene_favorites_cb, app);
    }
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneFavorites));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

static void radiogeddon_scene_favorites_add(RadioGeddonApp* app, uint32_t hz) {
    RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
    switch(radiogeddon_favorites_add(fav, hz)) {
    case RadioGeddonFavoriteAdded:
        if(radiogeddon_favorites_save(app->storage, fav)) {
            notification_message(app->notifications, &sequence_success);
        } else {
            radiogeddon_scene_show_message(
                app, "Save failed", "Could not write the\nfavorites file.");
            return;
        }
        break;
    case RadioGeddonFavoriteExists:
        radiogeddon_scene_show_message(
            app, "Already saved", "That frequency is\nalready a favorite.");
        return;
    case RadioGeddonFavoriteFull:
        radiogeddon_scene_show_message(
            app, "List full", "At most 24 favorites.\nDelete one first.");
        return;
    case RadioGeddonFavoriteInvalid:
        radiogeddon_scene_show_message(
            app, "Cannot add", "No supported radio\ntunes that frequency.");
        return;
    }
    radiogeddon_scene_favorites_on_enter(app);
}

bool radiogeddon_scene_favorites_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
    if(event.event >= FavoritesIndexFirst + fav->count) return false;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFavorites, event.event);

    if(event.event == FavoritesIndexAddCurrent) {
        radiogeddon_scene_favorites_add(app, app->frequency);
    } else if(event.event == FavoritesIndexAddTyped) {
        app->freq_target = RadioGeddonFreqTargetFavorite;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFrequency, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFrequency);
    } else {
        app->favorite_index = event.event - FavoritesIndexFirst;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFavoriteOptions);
    }
    return true;
}

void radiogeddon_scene_favorites_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
