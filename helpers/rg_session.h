/**
 * @file rg_session.h
 * @brief Full edition: research sessions (named groups of recordings).
 *
 * A session is a small text file in apps_data/radiogeddon/sessions/:
 *
 *     # RadioGeddon session
 *     Version: 1
 *     Name: Garage remote
 *     Created: 2026-10-11 12:00
 *     Signal: garage_btn1.sub
 *     Signal: garage_btn2.sub
 *
 * It only names recordings in the signals folder; the .sub files stay as
 * they are (the stock Sub-GHz app opens them as before), and deleting a
 * session never deletes a recording. Unknown lines are kept order-free:
 * ignored on reading, so a newer version's extra lines do not break this one.
 *
 * Saving is transactional: the new text is written to "<file>.tmp" and read
 * back; then the old file is renamed to "<file>.bak", the new one into
 * place, and the backup removed. Interrupted at any step, loading finds a
 * complete file: the session itself, else its backup.
 *
 * Grouping suggestions (rg_session_suggest) put recordings together by
 * frequency, protocol, frame length and capture time. They are suggestions:
 * nothing is grouped, merged or deleted without the user.
 *
 * The file operations are abstract, so this file has no SDK includes and the
 * failure paths are host-tested (test/test_session.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_SESSION_NAME_MAX   32u // display name, with its NUL
#define RG_SESSION_SIGNAL_MAX 68u // recording file name, with its NUL
#define RG_SESSION_SIGNALS    24u
#define RG_SESSION_TEXT_MAX   (RG_SESSION_SIGNALS * (RG_SESSION_SIGNAL_MAX + 9u) + 256u)
#define RG_SESSION_VERSION    1u

typedef struct {
    char name[RG_SESSION_NAME_MAX];
    char created[20]; // "YYYY-MM-DD HH:MM", or ""
    uint8_t count;
    char signal[RG_SESSION_SIGNALS][RG_SESSION_SIGNAL_MAX];
} RgSession;

void rg_session_init(RgSession* s, const char* name, const char* created);

/** A name for a session: 1-31 printable ASCII characters, not all spaces,
 * without / \ : * ? " < > | (it also names the file). */
bool rg_session_name_valid(const char* name);

/** A recording name a session can hold: a file name ending in ".sub". */
bool rg_session_signal_valid(const char* file);

typedef enum {
    RgSessionAddOk,
    RgSessionAddAlready, // already in the session (nothing changed)
    RgSessionAddFull,
    RgSessionAddInvalid,
} RgSessionAdd;

RgSessionAdd rg_session_add(RgSession* s, const char* file);
bool rg_session_remove(RgSession* s, const char* file);
int rg_session_find(const RgSession* s, const char* file);
/** A recording was renamed: every session entry naming @p from now names @p to. */
bool rg_session_rename_signal(RgSession* s, const char* from, const char* to);

/** Text of the session file; returns its length (0 if @p size is too small). */
size_t rg_session_write(const RgSession* s, char* out, size_t size);

typedef enum {
    RgSessionParseOk,
    RgSessionParseNotSession, // no "# RadioGeddon session" header
    RgSessionParseNoName,
} RgSessionParse;

/** Read a session file's text (any line endings). Bad Signal lines are skipped. */
RgSessionParse rg_session_parse(RgSession* s, const char* text, size_t len);

/* ---- transactional save ---------------------------------------------------- */

typedef struct {
    bool (*exists)(void* ctx, const char* path);
    bool (*remove)(void* ctx, const char* path);
    bool (*rename)(void* ctx, const char* from, const char* to);
    /* Write @p len bytes as the whole file; false on any error. */
    bool (*write)(void* ctx, const char* path, const char* data, size_t len);
    /* Read up to @p max bytes of the file; returns the length, or -1. */
    long (*read)(void* ctx, const char* path, char* buf, size_t max);
    void* ctx;
} RgSessionFs;

/**
 * Save @p s to @p path (a ".txt" file) transactionally. @p scratch must hold
 * 2 x RG_SESSION_TEXT_MAX bytes (the text and its read-back). False if any
 * step failed; the previous file (or its backup) is then still complete.
 */
bool rg_session_save(const RgSessionFs* fs, const char* path, const RgSession* s, char* scratch);

typedef enum {
    RgSessionLoadOk,
    RgSessionLoadRecovered, // the session was restored from its backup
    RgSessionLoadMissing,
    RgSessionLoadBad, // unreadable or not a session
} RgSessionLoad;

/**
 * Load @p path, finishing an interrupted save first: a leftover ".tmp" is
 * removed; a missing or unreadable file with a complete ".bak" is restored
 * from it. @p scratch holds RG_SESSION_TEXT_MAX bytes.
 */
RgSessionLoad
    rg_session_load(const RgSessionFs* fs, const char* path, RgSession* s, char* scratch);

/* ---- grouping suggestions ---------------------------------------------------- */

typedef struct {
    const char* file; // file name
    uint32_t frequency; // Hz (0 unknown)
    const char* protocol; // "RAW" or a decoder name ("" unknown)
    uint16_t bits; // decoded frame length (0 for RAW / unknown)
    uint32_t mtime; // seconds (0 unknown)
} RgSessionItem;

/* Recordings this close in frequency, and saved within this time of the
 * group's previous one, are suggested together. */
#define RG_SESSION_GROUP_HZ      50000u
#define RG_SESSION_GROUP_SECONDS (30u * 60u)
#define RG_SESSION_MAX_GROUPS    8u

typedef struct {
    uint8_t count; // recordings in the group
    uint16_t first; // index of the group's first item (by time)
    uint32_t frequency;
    char protocol[16];
    uint16_t bits;
} RgSessionGroup;

/**
 * Suggest groups of at least two recordings with the same frequency (within
 * RG_SESSION_GROUP_HZ), the same protocol and frame length, each saved within
 * RG_SESSION_GROUP_SECONDS of the group's previous one. @p items must be in
 * time order (either direction; unknown times only join on the other rules).
 * Writes each item's group index (or -1) to @p group_of. Returns the number
 * of groups (at most RG_SESSION_MAX_GROUPS, largest first).
 */
size_t rg_session_suggest(
    const RgSessionItem* items,
    size_t count,
    int8_t* group_of,
    RgSessionGroup* groups);
