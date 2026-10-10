#include "radiogeddon_scene.h"

#if RG_FEATURE_RANGE_SCAN

// Full edition: saved range-scan profiles. Scene state 0 loads the chosen
// profile into the Range Scanner setup; 1 deletes it after asking.

typedef enum {
    ProfilesEventDeleteYes = 950,
    ProfilesEventDeleteNo,
    ProfilesEventPickBase = 960, // + list row
} ProfilesEvent;

/* The names live only while this screen is open. */
static char (*profiles_names)[RADIOGEDDON_PROFILE_NAME_LEN];
static size_t profiles_count;

static void radiogeddon_scene_profiles_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ProfilesEventPickBase + index);
}

static bool radiogeddon_scene_profiles_deleting(RadioGeddonApp* app) {
    return scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneProfiles) == 1;
}

static void radiogeddon_scene_profiles_show(RadioGeddonApp* app) {
    if(!profiles_names) {
        profiles_names = malloc(RADIOGEDDON_PROFILES_MAX * RADIOGEDDON_PROFILE_NAME_LEN);
    }
    profiles_count =
        radiogeddon_profile_list(app->storage, profiles_names, RADIOGEDDON_PROFILES_MAX);
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(
        submenu, radiogeddon_scene_profiles_deleting(app) ? "Delete profile" : "Load profile");
    for(size_t i = 0; i < profiles_count; i++) {
        submenu_add_item(submenu, profiles_names[i], i, radiogeddon_scene_profiles_cb, app);
    }
    if(profiles_count == 0) {
        submenu_add_item(submenu, "(no saved profiles)", RADIOGEDDON_PROFILES_MAX, NULL, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_profiles_on_enter(void* context) {
    radiogeddon_scene_profiles_show(context);
}

bool radiogeddon_scene_profiles_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == ProfilesEventDeleteYes) {
        if(radiogeddon_profile_delete(app->storage, app->profile_name)) {
            notification_message(app->notifications, &sequence_success);
        } else {
            notification_message(app->notifications, &sequence_error);
        }
        radiogeddon_scene_profiles_show(app);
        return true;
    }
    if(event.event == ProfilesEventDeleteNo) {
        radiogeddon_scene_profiles_show(app);
        return true;
    }
    if(event.event < ProfilesEventPickBase) return false;
    size_t index = event.event - ProfilesEventPickBase;
    if(index >= profiles_count) return true;
    strlcpy(app->profile_name, profiles_names[index], sizeof(app->profile_name));

    if(radiogeddon_scene_profiles_deleting(app)) {
        furi_string_printf(app->temp_str, "%s\nThis cannot be undone.", app->profile_name);
        radiogeddon_scene_show_confirm(
            app,
            "Delete profile?",
            furi_string_get_cstr(app->temp_str),
            "Delete",
            ProfilesEventDeleteYes,
            ProfilesEventDeleteNo);
        return true;
    }

    RadioGeddonScanProfile p;
    if(!radiogeddon_profile_load(app->storage, app->profile_name, &p)) {
        radiogeddon_scene_show_message(
            app, "Cannot load", "The profile file is\ndamaged or from a\nnewer version.");
        return true;
    }
    app->settings.range_start_hz = p.start_hz;
    app->settings.range_end_hz = p.end_hz;
    app->settings.range_step_hz = p.step_hz;
    app->settings.range_dwell_ms = p.dwell_ms;
    app->settings.scan_threshold_db = p.threshold_db;
    app->settings.range_hold_on_hit = p.hold_on_hit;
    app->preset_index = p.preset_index;
    notification_message(app->notifications, &sequence_blink_green_10);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void radiogeddon_scene_profiles_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
    free(profiles_names);
    profiles_names = NULL;
    profiles_count = 0;
}

#endif
