#include "radiogeddon.h"

#define TAG "RadioGeddon"

static bool radiogeddon_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    RadioGeddon* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool radiogeddon_back_event_callback(void* context) {
    furi_assert(context);
    RadioGeddon* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void radiogeddon_tick_event_callback(void* context) {
    furi_assert(context);
    RadioGeddon* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

void radiogeddon_notify(RadioGeddon* app, const NotificationSequence* sequence) {
    if(app && app->notifications && sequence) {
        notification_message(app->notifications, sequence);
    }
}

static RadioGeddon* radiogeddon_alloc(void) {
    RadioGeddon* app = malloc(sizeof(RadioGeddon));
    memset(app, 0, sizeof(RadioGeddon));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->file_path = furi_string_alloc();
    app->compare_a = furi_string_alloc();
    app->compare_b = furi_string_alloc();
    app->freq_index = rg_frequencies_default_index;
    app->preset_index = rg_radio_preset_default_index;

    app->radio = rg_radio_alloc();
    rg_radio_set_frequency(app->radio, rg_frequencies[app->freq_index].frequency);
    rg_radio_set_preset(app->radio, rg_radio_presets[app->preset_index].preset);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&radiogeddon_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, radiogeddon_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, radiogeddon_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, radiogeddon_tick_event_callback, RG_TICK_PERIOD_MS);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewVariableItemList,
        variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewPopup, popup_get_view(app->popup));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewTextInput, text_input_get_view(app->text_input));

    app->receiver_view = rg_receiver_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewReceiver,
        rg_receiver_view_get_view(app->receiver_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void radiogeddon_free(RadioGeddon* app) {
    furi_assert(app);

    /* Make sure the radio is fully stopped and released before freeing. */
    rg_radio_free(app->radio);

    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewVariableItemList);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewReceiver);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    popup_free(app->popup);
    text_input_free(app->text_input);
    rg_receiver_view_free(app->receiver_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->file_path);
    furi_string_free(app->compare_a);
    furi_string_free(app->compare_b);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t radiogeddon_app(void* p) {
    UNUSED(p);
    RadioGeddon* app = radiogeddon_alloc();

    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    radiogeddon_free(app);
    return 0;
}
