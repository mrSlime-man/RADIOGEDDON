#include "radiogeddon.h"
#include "scenes/radiogeddon_scene.h"
#include "helpers/rg_memstat.h"

#include <toolbox/version.h>

static bool radiogeddon_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    RadioGeddonApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool radiogeddon_back_event_callback(void* context) {
    furi_assert(context);
    RadioGeddonApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void radiogeddon_tick_event_callback(void* context) {
    furi_assert(context);
    RadioGeddonApp* app = context;
    radiogeddon_memdiag_sample(NULL);
    scene_manager_handle_tick_event(app->scene_manager);
}

void radiogeddon_app_save_settings(RadioGeddonApp* app) {
    app->settings.frequency = app->frequency;
    app->settings.preset_index = app->preset_index;
    radiogeddon_settings_save(app->storage, &app->settings);
}

RadioGeddonApp* radiogeddon_app_alloc(void) {
    RadioGeddonApp* app = malloc(sizeof(RadioGeddonApp));
    memset(app, 0, sizeof(RadioGeddonApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);

    radiogeddon_storage_ensure_paths(app->storage);
    // A capture cut off by a reboot or flat battery left its temporary file
    // behind; nothing can name it any more.
    storage_common_remove(app->storage, RADIOGEDDON_RECORD_TEMP);

    app->file_path = furi_string_alloc();
    app->file_path_b = furi_string_alloc();
    app->temp_str = furi_string_alloc();
    radiogeddon_settings_load(app->storage, &app->settings);
    app->fw_tag = rg_mem_firmware_tag(version_get_version(NULL), version_get_githash(NULL));
    app->frequency = app->settings.frequency;
    app->preset_index = app->settings.preset_index;
    radiogeddon_loaded_signal_init(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded_b);

    app->subghz = radiogeddon_subghz_alloc();
    // An external module is used only if it answers now; otherwise the
    // internal radio is used and Settings shows that.
    if(app->settings.radio_external) {
        app->settings.radio_external =
            radiogeddon_subghz_set_radio(
                app->subghz, RadioGeddonRadioExternal, app->settings.ext_power) ==
            RadioGeddonRadioExternal;
    }
    radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    app->history = radiogeddon_history_alloc();
    app->history_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&radiogeddon_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, radiogeddon_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, radiogeddon_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, radiogeddon_tick_event_callback, 100);

    // Shared GUI modules
    app->submenu = submenu_alloc();
    app->widget = widget_alloc();
    app->var_item_list = variable_item_list_alloc();
    app->popup = popup_alloc();
    app->text_input = text_input_alloc();
    app->text_box = text_box_alloc();
    app->scanner_view = radiogeddon_scanner_view_alloc();
    app->receiver_view = radiogeddon_receiver_view_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewVarItemList,
        variable_item_list_get_view(app->var_item_list));
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewPopup, popup_get_view(app->popup));
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewTextInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewTextBox, text_box_get_view(app->text_box));
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewScanner,
        radiogeddon_scanner_view_get_view(app->scanner_view));
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewReceiver,
        radiogeddon_receiver_view_get_view(app->receiver_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

void radiogeddon_app_free(RadioGeddonApp* app) {
    furi_assert(app);

    // Ensure the radio is quiescent before tearing down.
    if(radiogeddon_subghz_is_rx_running(app->subghz)) radiogeddon_subghz_rx_stop(app->subghz);
    if(radiogeddon_subghz_is_tx_running(app->subghz)) radiogeddon_subghz_tx_stop(app->subghz);

    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewTextBox);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewScanner);
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewReceiver);
    radiogeddon_scene_db_release(app);

    submenu_free(app->submenu);
    widget_free(app->widget);
    variable_item_list_free(app->var_item_list);
    popup_free(app->popup);
    text_input_free(app->text_input);
    text_box_free(app->text_box);
    radiogeddon_scanner_view_free(app->scanner_view);
    radiogeddon_receiver_view_free(app->receiver_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    if(app->scanner) radiogeddon_scanner_free(app->scanner);
    if(app->hopper) radiogeddon_hopper_free(app->hopper);
    radiogeddon_history_free(app->history);
    furi_mutex_free(app->history_mutex);
    radiogeddon_subghz_free(app->subghz);

    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_reset(&app->loaded_b);
    furi_string_free(app->file_path);
    furi_string_free(app->file_path_b);
    furi_string_free(app->temp_str);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t radiogeddon_app(void* p) {
    UNUSED(p);
    radiogeddon_memdiag_start();
    RadioGeddonApp* app = radiogeddon_app_alloc();
    radiogeddon_memdiag_sample("app start");

    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    radiogeddon_app_free(app);
    radiogeddon_memdiag_log();
    return 0;
}
