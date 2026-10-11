/**
 * @file radiogeddon_room_view.h
 * @brief Dolphin Lab room (Full edition, a module): a small lab to walk
 * through with Left/Right.
 *
 * Stations: the radar (an illustration, labelled so; OK opens the real
 * Scanner), the oscilloscope (a saved RAW recording's first pulses, as
 * recorded), the antenna (OK opens Receive), the recorder (the number of
 * recordings; OK opens the Database), the terminal (counts read from the SD
 * card, firmware and version) and the gallery (Up/Down: every pose of the
 * mascot). Nothing here uses the radio; only OK on a station opens a tool.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <gui/view.h>
#include "../helpers/rg_mascot.h"

typedef struct RadioGeddonRoomView RadioGeddonRoomView;

typedef enum {
    RadioGeddonRoomRadar,
    RadioGeddonRoomScope,
    RadioGeddonRoomAntenna,
    RadioGeddonRoomRecorder,
    RadioGeddonRoomTerminal,
    RadioGeddonRoomGallery,
    RadioGeddonRoomCount,
} RadioGeddonRoomStation;

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
/** Count the recordings, saved reports and sessions on the SD card, and
 * show them with firmware @p fw (version_get_version) and the app's version. */
void radiogeddon_room_load_terminal(
    RadioGeddonRoomView* instance,
    Storage* storage,
    const char* fw);
/** Read the first RAW samples of @p path into the oscilloscope. */
void radiogeddon_room_load_scope(RadioGeddonRoomView* instance, Storage* storage, const char* path);

uint8_t radiogeddon_room_view_get_station(RadioGeddonRoomView* instance);
/** The pose shown in the gallery (RgMascotState). */
uint8_t radiogeddon_room_view_get_gallery(RadioGeddonRoomView* instance);

/** Advance to @p now with the dolphin's @p frame; redraws only on a change. */
void radiogeddon_room_view_update(
    RadioGeddonRoomView* instance,
    const RgMascotFrame* frame,
    uint32_t now);
