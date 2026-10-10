#include "radiogeddon_scene.h"

// One On/Off row per known frequency; the scanner sweeps the enabled ones.

static void radiogeddon_scene_scan_list_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t on = variable_item_get_current_value_index(item);
    uint8_t row = variable_item_list_get_selected_item_index(app->var_item_list);
    if(row >= radiogeddon_frequencies_count || row >= 32) return;
    uint32_t bit = 1u << row;
    if(on) {
        app->settings.scan_mask |= bit;
    } else if(app->settings.scan_mask != bit) {
        app->settings.scan_mask &= ~bit;
    } else {
        // Keep at least one frequency enabled.
        variable_item_set_current_value_index(item, 1);
        on = 1;
    }
    variable_item_set_current_value_text(item, on ? "On" : "Off");
}

void radiogeddon_scene_scan_list_on_enter(void* context) {
    RadioGeddonApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);

    char label[16];
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        uint32_t hz = radiogeddon_frequencies[i];
        snprintf(
            label,
            sizeof(label),
            "%lu.%03lu MHz",
            (unsigned long)(hz / 1000000),
            (unsigned long)((hz % 1000000) / 1000));
        VariableItem* item =
            variable_item_list_add(list, label, 2, radiogeddon_scene_scan_list_changed, app);
        bool on = app->settings.scan_mask & (1u << i);
        variable_item_set_current_value_index(item, on ? 1 : 0);
        variable_item_set_current_value_text(item, on ? "On" : "Off");
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_scan_list_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_scan_list_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_app_save_settings(app);
    variable_item_list_reset(app->var_item_list);
}
