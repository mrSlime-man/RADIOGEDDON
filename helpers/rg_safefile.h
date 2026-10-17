/**
 * @file rg_safefile.h
 * @brief Pure crash-safe file replacement: a new version or the old, never neither.
 *
 * The firmware's storage_common_rename() is not atomic: it removes the
 * destination, copies the source over and then deletes the source (Official
 * 1.4.3). A save written to "X.tmp" and renamed over "X" therefore has a
 * window where X is missing or cut off and only the .tmp is complete, and a
 * loader that reads only X then falls back to defaults: both versions lost.
 *
 * rg_safe_commit() closes that window with a commit marker, an empty file
 * created once the .tmp is known complete:
 *
 *   1. (caller) write and close X.tmp     a cut-off .tmp has no marker
 *   2. create the marker X.cmt            from here the .tmp is complete
 *   3. rename X.tmp -> X                  remove + copy + delete on Official
 *   4. remove the marker
 *
 * rg_safe_recover(), run before every load and every save, finishes or
 * discards an interrupted save: a marker means step 3 may not have finished,
 * so it is done again (roll forward: the .tmp is complete); a .tmp without a
 * marker is a write that never finished, so it is removed and X, untouched,
 * is the last complete version. Unlike a backup copy of X this costs no
 * second copy of the file on firmware whose rename copies.
 *
 * No SDK headers: the file system is reached through RgSafeFs, so the host
 * tests drive every step against a simulated card that loses power at each
 * point of a save, rename copying byte by byte (test_safefile).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool (*exists)(void* ctx, const char* path);
    /* True if the file is gone afterwards (removed, or was never there). */
    bool (*remove)(void* ctx, const char* path);
    /* Replace @p to with @p from (the firmware's rename); true on success. */
    bool (*rename)(void* ctx, const char* from, const char* to);
    /* Create an empty file (replacing any); true on success. */
    bool (*touch)(void* ctx, const char* path);
    void* ctx;
} RgSafeFs;

typedef struct {
    const char* path; /* the file itself */
    const char* tmp; /* the new version while it is written */
    const char* marker; /* exists while the new version is being put in place */
} RgSafeFile;

typedef enum {
    RgSafeClean, /* nothing was left over */
    RgSafeDiscarded, /* an unfinished write was removed; the file is the old version */
    RgSafeRolledForward, /* an interrupted commit was finished; the file is the new version */
    RgSafeStuck, /* a commit could not be finished now; the .tmp is kept for the next try */
} RgSafeRecover;

/** Finish or discard an interrupted save of @p f (see above). Idempotent. */
RgSafeRecover rg_safe_recover(const RgSafeFs* fs, const RgSafeFile* f);

/**
 * Call before writing f->tmp: recovers first, so a leftover marker can never
 * vouch for a .tmp that is only half written. False if the file is stuck
 * (a commit that cannot be finished): do not overwrite its .tmp then.
 */
bool rg_safe_begin(const RgSafeFs* fs, const RgSafeFile* f);

/**
 * Put the completely written and closed f->tmp in place of f->path (steps
 * 2-4). On failure before the marker exists, the .tmp is removed and the file
 * is unchanged; after it, the .tmp and marker stay, so rg_safe_recover()
 * finishes the replacement later. The caller never removes the .tmp after
 * calling this.
 */
bool rg_safe_commit(const RgSafeFs* fs, const RgSafeFile* f);
