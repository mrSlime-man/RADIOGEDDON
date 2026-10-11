/**
 * @file radiogeddon_room_view.h
 * @brief Dolphin Lab room 2.0 (Full edition, a module): a small lab to walk
 * through with Left/Right; every station opens a real tool.
 *
 * Stations: the radio workbench (the recording in context and the active
 * session; OK opens the recording, or the Database), the oscilloscope (that
 * recording's first pulses as stored, or another RAW chosen with OK), the
 * radar (an illustration, labelled so; OK opens the real Scanner), the
 * research terminal (the Research Journal's figures, firmware and version;
 * OK opens the journal), the equipment rack (Up/Down: Scanner, Adaptive
 * Scanner, Activity Logger, Receive; OK opens it) and the dolphin corner
 * (the dolphin as customised, Up/Down: every pose; OK: customization).
 * Nothing here uses the radio, and the terminal reads no folder: its
 * figures are the journal's counts.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <gui/view.h>
#include "../helpers/rg_mascot.h"

typedef struct RadioGeddonRoomView RadioGeddonRoomView;

typedef enum {
    RadioGeddonRoomWorkbench,
    RadioGeddonRoomScope,
    RadioGeddonRoomRadar,
    RadioGeddonRoomTerminal,
    RadioGeddonRoomRack,
    RadioGeddonRoomCorner,
    RadioGeddonRoomCount,
} RadioGeddonRoomStation;

/* The equipment rack's tools (RadioGeddonRoomRack). */
typedef enum {
    RadioGeddonRackScanner,
    RadioGeddonRackAdaptive,
    RadioGeddonRackLogger,
    RadioGeddonRackReceive,
    RadioGeddonRackCount,
} RadioGeddonRackTool;

/* OK on @p station. */
typedef void (*RadioGeddonRoomCallback)(uint8_t station, void* context);

#define RADIOGEDDON_ROOM_LINES    5
#define RADIOGEDDON_ROOM_LINE_LEN 26
#define RADIOGEDDON_ROOM_SCOPE_W  80
/** The oscilloscope's trace: one bit per column, the carrier's level there. */
typedef struct {
    bool loaded;
    bool empty; // the file had no RAW samples
    uint8_t bits[RADIOGEDDON_ROOM_SCOPE_W / 8];
    uint32_t span_us; // time across the trace
    char name[24];
} RadioGeddonRoomTrace;

RadioGeddonRoomView* radiogeddon_room_view_alloc(void);
void radiogeddon_room_view_free(RadioGeddonRoomView* instance);
View* radiogeddon_room_view_get_view(RadioGeddonRoomView* instance);
void radiogeddon_room_view_set_callback(
    RadioGeddonRoomView* instance,
    RadioGeddonRoomCallback callback,
    void* context);

/** The terminal's lines (copied), and the recorder's count. */
void radiogeddon_room_view_set_terminal(
    RadioGeddonRoomView* instance,
    const char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN],
    uint32_t recordings);
void radiogeddon_room_view_set_scope(
    RadioGeddonRoomView* instance,
    const RadioGeddonRoomTrace* scope);

#include <storage/storage.h>
#include "../helpers/rg_journal.h"
/** The research terminal: the journal's level and counts, firmware @p fw
 * (version_get_version) and the app's version. Reads nothing from the card. */
void radiogeddon_room_load_terminal(
    RadioGeddonRoomView* instance,
    const RgJournal* journal,
    const char* fw);

/** The workbench: the recording in context and the active session ("" none). */
void radiogeddon_room_view_set_workbench(
    RadioGeddonRoomView* instance,
    const char* recording,
    const char* session);

/** The lab decoration shown in the corner (RgDecoration, 0: none). */
void radiogeddon_room_view_set_decoration(RadioGeddonRoomView* instance, uint8_t decoration);

/** The rack's selected tool (RadioGeddonRackTool). */
uint8_t radiogeddon_room_view_get_rack(RadioGeddonRoomView* instance);
/** Read the first RAW samples of @p path into the oscilloscope. */
void radiogeddon_room_load_scope(RadioGeddonRoomView* instance, Storage* storage, const char* path);

uint8_t radiogeddon_room_view_get_station(RadioGeddonRoomView* instance);
/** The pose shown in the corner's gallery (RgMascotState). */
uint8_t radiogeddon_room_view_get_gallery(RadioGeddonRoomView* instance);

/** Advance to @p now with the dolphin's @p frame; redraws only on a change. */
void radiogeddon_room_view_update(
    RadioGeddonRoomView* instance,
    const RgMascotFrame* frame,
    uint32_t now);
