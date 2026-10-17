#include "radiogeddon_scene.h"

#if RG_FEATURE_ADAPTIVE_SCAN

/*
 * Full edition: the Adaptive Scanner's setup. The Scanner itself runs the
 * schedule (helpers/rg_adapt.h): Balanced and Focused favour frequencies
 * that have shown activity, Custom also favours the favorites; every
 * frequency is still revisited within a bounded number of visits. The mode
 * is kept in the settings; what was learned lasts while the Scanner's
 * results do (until the main menu) and can be reset here.
 */

typedef enum {
    AdaptiveRowMode,
    AdaptiveRowStart,
    AdaptiveRowReset,
    AdaptiveRowAbout,
} AdaptiveRow;

typedef enum {
    AdaptiveEventStart = 1200,
    AdaptiveEventReset,
} AdaptiveEvent;

static const RgAdaptMode adaptive_modes[] = {RgAdaptBalanced, RgAdaptFocused, RgAdaptCustom};
#define ADAPTIVE_MODES COUNT_OF(adaptive_modes)

static uint8_t adaptive_mode_index(RadioGeddonApp* app) {
    for(size_t i = 0; i < ADAPTIVE_MODES; i++)
        if(adaptive_modes[i] == app->settings.scan_mode) return (uint8_t)i;
    return 0;
}

static void adaptive_mode_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.scan_mode = (uint8_t)adaptive_modes[i];
    variable_item_set_current_value_text(item, rg_adapt_mode_name(adaptive_modes[i]));
}

static void adaptive_enter(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    if(index == AdaptiveRowStart)
        view_dispatcher_send_custom_event(app->view_dispatcher, AdaptiveEventStart);
    if(index == AdaptiveRowReset)
        view_dispatcher_send_custom_event(app->view_dispatcher, AdaptiveEventReset);
}

void radiogeddon_scene_adaptive_on_enter(void* context) {
    RadioGeddonApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    uint8_t mi = adaptive_mode_index(app);
    app->settings.scan_mode = (uint8_t)adaptive_modes[mi];
    VariableItem* item =
        variable_item_list_add(list, "Mode", ADAPTIVE_MODES, adaptive_mode_changed, app);
    variable_item_set_current_value_index(item, mi);
    variable_item_set_current_value_text(item, rg_adapt_mode_name(adaptive_modes[mi]));
    variable_item_list_add(list, "Start scanner", 1, NULL, app);
    variable_item_list_add(list, "Reset learning", 1, NULL, app);
    item = variable_item_list_add(list, "Custom: favorites", 1, NULL, app);
    variable_item_set_current_value_text(item, "first");
    variable_item_list_set_enter_callback(list, adaptive_enter, app);
    variable_item_list_set_selected_item(list, AdaptiveRowStart);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_adaptive_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == AdaptiveEventStart) {
        radiogeddon_app_save_settings(app);
        app->scan_mode_run = app->settings.scan_mode;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        return true;
    }
    if(event.event == AdaptiveEventReset) {
        if(app->scanner) {
            radiogeddon_scanner_reset_learning(app->scanner);
            radiogeddon_scanner_reset_peaks(app->scanner);
        }
        notification_message(app->notifications, &sequence_blink_blue_10);
        return true;
    }
    return false;
}

void radiogeddon_scene_adaptive_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_app_save_settings(app);
    radiogeddon_scene_var_list_release(app);
}

#endif
