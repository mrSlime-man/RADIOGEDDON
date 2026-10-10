#include "radiogeddon_scene.h"

// Type a custom receive frequency in kHz (Settings, OK on Frequency). The
// radio in use decides whether it can tune it; transmitting is still checked
// against the firmware's region rules when a recording is replayed.

typedef enum {
    FrequencyEventEntered = 700,
} FrequencyEvent;

static void radiogeddon_scene_frequency_entered(void* context, int32_t number) {
    RadioGeddonApp* app = context;
    // The keyboard only returns numbers within the range it was given.
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFrequency, (uint32_t)number);
    view_dispatcher_send_custom_event(app->view_dispatcher, FrequencyEventEntered);
}

void radiogeddon_scene_frequency_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(!app->number_input) {
        app->number_input = number_input_alloc();
        view_dispatcher_add_view(
            app->view_dispatcher,
            RadioGeddonViewNumberInput,
            number_input_get_view(app->number_input));
    }
    // Settings clears the state; after a refused entry it holds that entry,
    // so it can be corrected rather than typed again.
    uint32_t khz = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneFrequency);
    if(khz == 0) khz = app->frequency / 1000u;
    number_input_set_header_text(app->number_input, "Frequency in kHz");
    number_input_set_result_callback(
        app->number_input,
        radiogeddon_scene_frequency_entered,
        app,
        (int32_t)khz,
        (int32_t)RG_FREQ_MIN_KHZ,
        (int32_t)RG_FREQ_MAX_KHZ);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewNumberInput);
}

bool radiogeddon_scene_frequency_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != FrequencyEventEntered) {
        return false;
    }
    uint32_t khz = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneFrequency);
    uint32_t hz = khz * 1000u;
    if(!rg_freq_in_range(hz) || !radiogeddon_subghz_is_frequency_allowed(app->subghz, hz)) {
        // Back from the message returns to the keyboard to try again.
        radiogeddon_scene_show_message(
            app,
            "Cannot tune there",
            "The radio in use cannot\ntune to that frequency.\nThe frequency was\nnot changed.");
        return true;
    }
    app->frequency = hz;
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void radiogeddon_scene_frequency_on_exit(void* context) {
    RadioGeddonApp* app = context;
    // Showing the message leaves this scene too; the keyboard is made again
    // on return, so nothing is kept while another screen is open.
    if(app->number_input) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewNumberInput);
        number_input_free(app->number_input);
        app->number_input = NULL;
    }
}
