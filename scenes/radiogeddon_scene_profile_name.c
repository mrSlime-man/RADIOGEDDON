#include "radiogeddon_scene.h"

#if RG_FEATURE_RANGE_SCAN

// Full edition: name the current Range Scanner setup and save it as a
// profile. An existing profile of that name is replaced only after asking.

typedef enum {
    ProfileNameEventEntered = 980,
    ProfileNameEventReplace,
    ProfileNameEventKeep,
} ProfileNameEvent;

static bool
    radiogeddon_scene_profile_name_validator(const char* text, FuriString* error, void* context) {
    UNUSED(context);
    RgDbNameError check = rg_db_check_name(text);
    if(check == RgDbNameOk && strlen(text) >= RADIOGEDDON_PROFILE_NAME_LEN)
        check = RgDbNameTooLong;
    if(check != RgDbNameOk) {
        furi_string_set(error, rg_db_name_error_text(check));
        return false;
    }
    return true;
}

static void radiogeddon_scene_profile_name_entered(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ProfileNameEventEntered);
}

static void radiogeddon_scene_profile_name_keyboard(RadioGeddonApp* app) {
    TextInput* text_input = app->text_input;
    text_input_reset(text_input);
    text_input_set_header_text(text_input, "Profile name");
    text_input_set_result_callback(
        text_input,
        radiogeddon_scene_profile_name_entered,
        app,
        app->profile_name,
        sizeof(app->profile_name),
        false);
    text_input_set_minimum_length(text_input, 1);
    text_input_set_validator(text_input, radiogeddon_scene_profile_name_validator, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

void radiogeddon_scene_profile_name_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(app->profile_name[0] == '\0') {
        // Suggest the range itself, e.g. "433.00-435.00".
        char lo[RG_FREQ_TEXT_SIZE], hi[RG_FREQ_TEXT_SIZE];
        rg_freq_text(app->settings.range_start_hz, lo, sizeof(lo));
        rg_freq_text(app->settings.range_end_hz, hi, sizeof(hi));
        snprintf(app->profile_name, sizeof(app->profile_name), "%s-%s", lo, hi);
    }
    radiogeddon_scene_profile_name_keyboard(app);
}

static void radiogeddon_scene_profile_name_save(RadioGeddonApp* app, bool overwrite) {
    RadioGeddonScanProfile p = {
        .start_hz = app->settings.range_start_hz,
        .end_hz = app->settings.range_end_hz,
        .step_hz = app->settings.range_step_hz,
        .dwell_ms = app->settings.range_dwell_ms,
        .threshold_db = app->settings.scan_threshold_db,
        .hold_on_hit = app->settings.range_hold_on_hit,
        .preset_index = app->preset_index,
    };
    if(radiogeddon_profile_save(app->storage, app->profile_name, &p, overwrite)) {
        notification_message(app->notifications, &sequence_success);
        scene_manager_previous_scene(app->scene_manager);
    } else {
        radiogeddon_scene_show_message(
            app, "Save failed", "Could not write the\nprofile to the SD card.");
    }
}

bool radiogeddon_scene_profile_name_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case ProfileNameEventEntered:
        if(radiogeddon_profile_exists(app->storage, app->profile_name)) {
            furi_string_printf(app->temp_str, "%s\nexists already.", app->profile_name);
            radiogeddon_scene_show_confirm(
                app,
                "Replace profile?",
                furi_string_get_cstr(app->temp_str),
                "Replace",
                ProfileNameEventReplace,
                ProfileNameEventKeep);
        } else {
            radiogeddon_scene_profile_name_save(app, false);
        }
        return true;
    case ProfileNameEventReplace:
        radiogeddon_scene_profile_name_save(app, true);
        return true;
    case ProfileNameEventKeep:
        radiogeddon_scene_profile_name_keyboard(app); // choose another name
        return true;
    default:
        return false;
    }
}

void radiogeddon_scene_profile_name_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_input_reset(app->text_input);
    widget_reset(app->widget);
}

#endif
