/**
 * @file radiogeddon_db.h
 * @brief Signal database: index of the .sub files in the signals folder.
 *
 * Builds an rg_db.h index by listing the folder and reading the first 512
 * bytes of each file (plus the whole file for RAW captures that share their
 * size with another one, to confirm duplicates). Memory is sized to the
 * listing and capped by the free heap; when the folder holds more files than
 * fit, the first ones are indexed and `truncated` is set. Allocate it only
 * while the Database is open.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include "rg_db.h"
#include "radiogeddon_progress.h"

typedef enum {
    RadioGeddonDbOk,
    RadioGeddonDbNoMemory,
} RadioGeddonDbStatus;

typedef struct {
    RgDb db;
    RgDbQuery query;
    uint16_t* view; /* entry indices matching the query, in order */
    size_t view_count;
    size_t total_files; /* .sub files in the folder */
    bool truncated; /* not all of them fit in memory */
} RadioGeddonDb;

/**
 * Index the signals folder. NULL (with RadioGeddonDbNoMemory) if nothing fits.
 * @p progress (may be NULL) is told how many files have been read.
 */
RadioGeddonDb* radiogeddon_db_load(
    Storage* storage,
    RadioGeddonDbStatus* status,
    RadioGeddonProgressCallback progress,
    void* context);

void radiogeddon_db_free(RadioGeddonDb* db);

/** Re-run the query after changing db->query. */
void radiogeddon_db_apply(RadioGeddonDb* db);

/** Entry at a position of the current view, or NULL. */
const RgDbEntry* radiogeddon_db_at(const RadioGeddonDb* db, size_t position);

/** Full path of an entry. */
void radiogeddon_db_path(const RadioGeddonDb* db, const RgDbEntry* e, FuriString* out);

/** "MM-DD HH:MM" for a file time, or "--" when unknown. */
void radiogeddon_db_format_time(uint32_t timestamp, char* out, size_t size);
