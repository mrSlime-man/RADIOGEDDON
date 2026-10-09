#include "../radiogeddon.h"

static void radiogeddon_settings_freq_changed(VariableItem* item) {
    RadioGeddon* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= rg_frequencies_count) index = 0;
    app->freq_index = index;
    variable_item_set_current_value_text(item, rg_frequencies[index].label);
    rg_radio_set_frequency(app->radio, rg_frequencies[index].frequency);
}

static void radiogeddon_settings_preset_changed(VariableItem* item) {
    RadioGeddon* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= rg_radio_presets_count) index = 0;
    app->preset_index = index;
    variable_item_set_current_value_text(item, rg_radio_presets[index].label);
    rg_radio_set_preset(app->radio, rg_radio_presets[index].preset);
}

void radiogeddon_scene_settings_on_enter(void* context) {
    RadioGeddon* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(
        list, "Frequency", (uint8_t)rg_frequencies_count, radiogeddon_settings_freq_changed, app);
    variable_item_set_current_value_index(item, (uint8_t)app->freq_index);
    variable_item_set_current_value_text(item, rg_frequencies[app->freq_index].label);

    item = variable_item_list_add(
        list,
        "Modulation",
        (uint8_t)rg_radio_presets_count,
        radiogeddon_settings_preset_changed,
        app);
    variable_item_set_current_value_index(item, (uint8_t)app->preset_index);
    variable_item_set_current_value_text(item, rg_radio_presets[app->preset_index].label);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVariableItemList);
}

bool radiogeddon_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_settings_on_exit(void* context) {
    RadioGeddon* app = context;
    variable_item_list_reset(app->var_item_list);
}
