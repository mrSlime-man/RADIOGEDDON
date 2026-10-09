#include "radiogeddon_scene.h"

static void radiogeddon_config_freq_text(uint32_t hz, char* out, size_t out_size) {
    snprintf(
        out,
        out_size,
        "%lu.%02lu",
        (unsigned long)(hz / 1000000),
        (unsigned long)((hz % 1000000) / 10000));
}

static void radiogeddon_scene_config_freq_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    uint32_t hz = radiogeddon_frequencies[index];
    char text[12];
    radiogeddon_config_freq_text(hz, text, sizeof(text));
    variable_item_set_current_value_text(item, text);
    app->frequency = hz;
}

static void radiogeddon_scene_config_preset_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, radiogeddon_presets[index].label);
    app->preset_index = index;
}

void radiogeddon_scene_config_on_enter(void* context) {
    RadioGeddonApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);

    // Frequency
    VariableItem* item = variable_item_list_add(
        list,
        "Frequency MHz",
        radiogeddon_frequencies_count,
        radiogeddon_scene_config_freq_changed,
        app);
    uint8_t freq_index = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count; i++) {
        if(radiogeddon_frequencies[i] == app->frequency) {
            freq_index = (uint8_t)i;
            break;
        }
    }
    variable_item_set_current_value_index(item, freq_index);
    char text[12];
    radiogeddon_config_freq_text(radiogeddon_frequencies[freq_index], text, sizeof(text));
    variable_item_set_current_value_text(item, text);

    // Preset
    item = variable_item_list_add(
        list,
        "Modulation",
        radiogeddon_presets_count,
        radiogeddon_scene_config_preset_changed,
        app);
    variable_item_set_current_value_index(item, app->preset_index);
    variable_item_set_current_value_text(item, radiogeddon_presets[app->preset_index].label);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_config_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_config_on_exit(void* context) {
    RadioGeddonApp* app = context;
    // Persist chosen config into the radio wrapper.
    radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    variable_item_list_reset(app->var_item_list);
}
