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
#include <gui/modules/number_input.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "radiogeddon_edition.h"
#include "helpers/radiogeddon_subghz.h"
#include "helpers/radiogeddon_history.h"
#include "helpers/radiogeddon_storage.h"
#include "helpers/radiogeddon_analysis.h"
#include "helpers/radiogeddon_settings.h"
#include "helpers/radiogeddon_scanner.h"
#include "helpers/radiogeddon_hopper.h"
#include "helpers/radiogeddon_report.h"
#include "helpers/radiogeddon_memdiag.h"
#include "helpers/rg_freq.h"
#include "helpers/rg_range.h"
#include "helpers/radiogeddon_profiles.h"
#include "helpers/radiogeddon_rangescan.h"
#include "views/radiogeddon_scanner_view.h"
#include "views/radiogeddon_receiver_view.h"
#include "views/radiogeddon_timeline_view.h"
#include "views/radiogeddon_db_view.h"
#include "views/radiogeddon_spectrum_view.h"
#include "views/radiogeddon_waterfall_view.h"
#include "views/radiogeddon_bits_view.h"

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
    RadioGeddonViewNumberInput, // added only while a custom frequency is typed
    RadioGeddonViewSpectrum, // Full: added only while the Range Scanner is open
    RadioGeddonViewWaterfall, // Full: added only while the Waterfall is open
    RadioGeddonViewBits, // Full: added only while the Bitstream Explorer is open
} RadioGeddonView;

/** What a frequency typed on the number keyboard is for. */
typedef enum {
    RadioGeddonFreqTargetReceive, // the receive frequency (must be tunable)
    RadioGeddonFreqTargetRangeStart, // range scan start (any supported value)
    RadioGeddonFreqTargetRangeEnd, // range scan end
    RadioGeddonFreqTargetFavorite, // a new favorite (must be tunable)
} RadioGeddonFreqTarget;

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
    // Number keyboard: exists only while a custom frequency is typed.
    NumberInput* number_input;

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
    // Percentage under a busy popup (radiogeddon_scene_show_progress): two
    // buffers, so the one on screen is never rewritten while it is drawn.
    char progress_text[2][8];
    // Message scene (radiogeddon_scene_show_message): static strings only.
    const char* message_header;
    const char* message_text;
    uint8_t progress_slot;
    uint8_t progress_pct;
    uint32_t progress_tick;
    // "Not enough memory" text shown before a radio session (with figures).
    char memory_text[96];
    // Why a transmission was refused (Replay), shown on its popup.
    char tx_text[96];
    uint32_t fw_tag; // identifies the running firmware (rg_mem_firmware_tag)

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
    // The next Receive start begins a RAW recording at once (long OK on a
    // scanner screen).
    bool receiver_autorecord;
    // What the frequency keyboard is editing (RadioGeddonFreqTarget).
    uint8_t freq_target;

#if RG_EDITION_FULL
    // Bands the radio in use accepts, measured when a Full screen needs them.
    RgBandSet bands;
    // Favorite frequencies, read from the card on first use.
    RadioGeddonFavorites favorites;
    bool favorites_loaded;
    size_t favorite_index; // favorite whose options are shown
    // Range scanner: plan of the current settings, the engine and its screen
    // (both exist only while the Range Scanner screen is open).
    RgRange range;
    RgRangeResult range_result;
    RadioGeddonRangeScan* rangescan;
    RadioGeddonSpectrumView* spectrum_view;
    uint32_t range_cursor; // point under the cursor, kept across a trip to Receive
    char profile_name[RADIOGEDDON_PROFILE_NAME_LEN];
#endif
#if RG_FEATURE_WATERFALL
    // Waterfall: the range engine above (app->rangescan) with a history
    // buffer and a screen of its own, all only while the Waterfall is open.
    RadioGeddonWaterfallView* waterfall_view;
    void* wf_buf;
    uint16_t wf_cursor; // column under the cursor, kept across a trip to Receive
    uint8_t wf_span_index; // sensitivity (radiogeddon_waterfall_spans)
    bool wf_noise_comp;
#endif
#if RG_FEATURE_BITSTREAM
    // Bitstream Explorer: the analysis of the open capture and its screen,
    // only while the explorer is open.
    RadioGeddonBitsView* bits_view;
    RgAnalysis* bits_doc;
#endif
};

/** Copy the session frequency/preset into settings and write them to the SD card. */
void radiogeddon_app_save_settings(RadioGeddonApp* app);

/** Allocate and free the full application context. */
RadioGeddonApp* radiogeddon_app_alloc(void);
void radiogeddon_app_free(RadioGeddonApp* app);
