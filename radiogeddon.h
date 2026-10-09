/**
 * @file radiogeddon.h
 * @brief RadioGeddon — standalone Sub-GHz radio analysis toolkit for Flipper Zero.
 *
 * Central application context and shared type definitions. The application is
 * built on the standard Flipper GUI stack (ViewDispatcher + SceneManager) and
 * a thin wrapper around the firmware's Sub-GHz subsystem (helpers/radiogeddon_subghz).
 */
#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "helpers/radiogeddon_subghz.h"
#include "helpers/radiogeddon_history.h"
#include "helpers/radiogeddon_storage.h"
#include "helpers/radiogeddon_analysis.h"
#include "helpers/radiogeddon_settings.h"
#include "helpers/radiogeddon_scanner.h"
#include "helpers/radiogeddon_hopper.h"
#include "helpers/radiogeddon_report.h"
#include "views/radiogeddon_scanner_view.h"
#include "views/radiogeddon_receiver_view.h"
#include "views/radiogeddon_timeline_view.h"
#include "views/radiogeddon_db_view.h"

#define RADIOGEDDON_TEXT_INPUT_BUFFER_SIZE 64
#define RADIOGEDDON_TAG                    "RadioGeddon"

/** Views owned by the application, addressed through the ViewDispatcher. */
typedef enum {
    RadioGeddonViewSubmenu,
    RadioGeddonViewWidget,
    RadioGeddonViewVarItemList,
    RadioGeddonViewPopup,
    RadioGeddonViewTextInput,
    RadioGeddonViewTextBox,
    RadioGeddonViewScanner,
    RadioGeddonViewReceiver,
    RadioGeddonViewTimeline, // added only while the Pulse Timeline is open
    RadioGeddonViewDb, // added only while the Database is open
} RadioGeddonView;

typedef struct RadioGeddonApp RadioGeddonApp;

struct RadioGeddonApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    Storage* storage;

    // Shared GUI modules
    Submenu* submenu;
    Widget* widget;
    VariableItemList* var_item_list;
    Popup* popup;
    TextInput* text_input;
    TextBox* text_box;

    // Custom live views
    RadioGeddonScannerView* scanner_view;
    RadioGeddonReceiverView* receiver_view;
    // Pulse Timeline: view and open file exist only while that scene runs.
    RadioGeddonTimelineView* timeline_view;
    RadioGeddonRawFile* timeline_file;
    // Database index and list: exist while the Database or a screen opened
    // from it is shown, and are freed on returning to the main menu.
    RadioGeddonDb* db;
    RadioGeddonDbView* db_view;
    bool db_keep; // leaving the list for a screen opened from it
    bool db_dirty; // files changed: re-index on return to the list

    // Radio subsystem wrapper
    RadioGeddonSubGhz* subghz;

    // Decoded-signal session history (volatile, cleared per receive session).
    // Written from the radio worker thread, read from the UI thread — guarded.
    RadioGeddonHistory* history;
    FuriMutex* history_mutex;

    // Text buffers / transient selection state
    char text_store[RADIOGEDDON_TEXT_INPUT_BUFFER_SIZE];
    FuriString* file_path; // currently selected / loaded .sub path
    FuriString* file_path_b; // second file for comparison
    FuriString* temp_str; // scratch for info rendering

    // Persisted settings (frequency/preset are mirrored in the fields below)
    RadioGeddonSettings settings;

    // Scanner results; allocated on first use of the scanner and kept until
    // the user returns to the main menu, so a trip to the receiver keeps them.
    RadioGeddonScanner* scanner;
    // Hopper statistics and activity history; same lifetime rule as scanner.
    RadioGeddonHopper* hopper;

    // Session configuration
    uint32_t frequency; // Hz
    uint8_t preset_index; // index into radiogeddon_preset table
    size_t selected_history_index;
    bool have_loaded_signal; // a signal loaded from file is available for analysis
    bool file_damaged; // the open Database file is not a readable .sub
    RadioGeddonLoadedSignal loaded; // parsed representation of a loaded .sub file
    RadioGeddonLoadedSignal loaded_b; // second parsed signal (comparison)

    // Pending save: true if saving a RAW capture, false if saving a decoded signal
    bool save_is_raw;
    // Set when leaving the receiver for the save screen, so returning to the
    // receiver keeps the session's decoded-signal list instead of clearing it.
    bool receiver_preserve_history;
    // True while the scanner sweep is actively probing a present radio.
    bool scanner_running;
};

/** Copy the session frequency/preset into settings and write them to the SD card. */
void radiogeddon_app_save_settings(RadioGeddonApp* app);

/** Allocate and free the full application context. */
RadioGeddonApp* radiogeddon_app_alloc(void);
void radiogeddon_app_free(RadioGeddonApp* app);
