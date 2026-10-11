/**
 * @file radiogeddon_sessions.h
 * @brief Full edition: research sessions on the SD card (rg_session.h).
 *
 * Sessions live in apps_data/radiogeddon/sessions/<name>.txt and name
 * recordings of the signals folder. The active session (its name in
 * sessions/active) collects the recordings saved from Receive.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_SESSIONS

#include <furi.h>
#include <storage/storage.h>
#include "rg_session.h"
#include "radiogeddon_storage.h"

#define RADIOGEDDON_SESSIONS_FOLDER RADIOGEDDON_APP_FOLDER "/sessions"
#define RADIOGEDDON_SESSIONS_MAX    16u

/** Heap a save takes for its text and read-back (on top of the session). */
#define RADIOGEDDON_SESSION_SCRATCH (2u * RG_SESSION_TEXT_MAX)

typedef struct {
    size_t count;
    bool truncated; // more session files than RADIOGEDDON_SESSIONS_MAX
    char name[RADIOGEDDON_SESSIONS_MAX][RG_SESSION_NAME_MAX];
} RadioGeddonSessionList;

/** Session names found in the folder (from their file names), sorted. */
void radiogeddon_sessions_list(Storage* storage, RadioGeddonSessionList* out);

/** Load session @p name (finishing an interrupted save first). */
RgSessionLoad radiogeddon_session_load(Storage* storage, const char* name, RgSession* s);

/** Save @p s under its own name, transactionally. False on a card error. */
bool radiogeddon_session_save(Storage* storage, const RgSession* s);

/** Whether a session file called @p name exists. */
bool radiogeddon_session_exists(Storage* storage, const char* name);

/**
 * Rename the session @p from to @p to (the new name must be free). The
 * recordings are untouched. False on a card error or a taken name.
 */
bool radiogeddon_session_rename(Storage* storage, RgSession* s, const char* to);

/** Delete the session file (and any backup); the recordings stay. */
bool radiogeddon_session_delete(Storage* storage, const char* name);

/** The active session's name ("" when none), and set or clear it. */
void radiogeddon_session_active(Storage* storage, char* out, size_t size);
bool radiogeddon_session_set_active(Storage* storage, const char* name);

/**
 * A recording was saved: if a session is active, add @p file to it.
 * Returns the session's name in @p out ("" when none or on failure).
 */
bool radiogeddon_session_capture_saved(Storage* storage, const char* file, char* out, size_t size);

/** A recording was renamed: every session naming @p from now names @p to. */
void radiogeddon_sessions_rename_signal(Storage* storage, const char* from, const char* to);

/** Whether a recording of the signals folder exists. */
bool radiogeddon_session_signal_exists(Storage* storage, const char* file);

/** "YYYY-MM-DD HH:MM" for now. */
void radiogeddon_session_now(char* out, size_t size);

#endif
