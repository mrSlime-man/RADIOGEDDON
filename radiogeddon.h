#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include "scenes/radiogeddon_scene.h"
#include "views/receiver_view.h"
#include "helpers/radio.h"
#include "helpers/frequencies.h"
#include "helpers/signal_storage.h"

#define RG_TEXT_STORE_SIZE 64
#define RG_VERSION_STRING  "1.0"

typedef enum {
    RadioGeddonViewSubmenu,
    RadioGeddonViewVariableItemList,
    RadioGeddonViewWidget,
    RadioGeddonViewPopup,
    RadioGeddonViewTextInput,
    RadioGeddonViewReceiver,
} RadioGeddonView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    Storage* storage;

    /* View modules */
    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;
    Popup* popup;
    TextInput* text_input;
    RgReceiverView* receiver_view;

    /* Radio */
    RgRadio* radio;

    /* Shared state */
    char text_store[RG_TEXT_STORE_SIZE];
    FuriString* file_path; /* selected / last saved file */
    FuriString* compare_a; /* first file chosen for comparison */
    FuriString* compare_b; /* second file chosen for comparison */
    size_t freq_index; /* index into rg_frequencies */
    size_t preset_index; /* index into rg_radio_presets */
    uint32_t detection_count; /* decoded signals this session */
    bool have_last_save; /* a recording was saved this session */
} RadioGeddon;

/* Shared helpers implemented in radiogeddon.c */
void radiogeddon_notify(RadioGeddon* app, const NotificationSequence* sequence);

/* Tick period (ms) driving live RSSI/sample refresh. */
#define RG_TICK_PERIOD_MS 200
