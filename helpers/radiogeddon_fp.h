/**
 * @file radiogeddon_fp.h
 * @brief Full edition (a module): the fingerprint index on the SD card and
 * Find Similar Signals.
 *
 * apps_data/radiogeddon/fingerprints.idx holds one fixed-size record per
 * recording of the signals folder (rg_fp.h): its name, size and timestamp
 * when fingerprinted, and the fingerprint. A search first brings the index
 * up to date in one streaming pass, written to fingerprints.tmp and renamed
 * over the old index only when complete:
 *
 *  - records whose file is gone, renamed, or changed (size or timestamp)
 *    are dropped; damaged records are skipped;
 *  - recordings without a record are fingerprinted, a batch of names at a
 *    time with the folder closed in between (one analysis in memory at a
 *    time); unreadable ones are counted and left out;
 *  - recordings are only read, never written.
 *
 * Then the index is streamed once more to rank every other recording
 * against the target (rg_fp_rank, the best RG_FP_TOP kept). Memory: one
 * RgAnalyzer while fingerprinting, the names kept (4 bytes each), a record.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_FINGERPRINT

#include <furi.h>
#include <storage/storage.h>
#include "rg_fp.h"
#include "radiogeddon_progress.h"

#define RADIOGEDDON_FP_INDEX EXT_PATH("apps_data/radiogeddon/fingerprints.idx")
#define RADIOGEDDON_FP_TEMP  EXT_PATH("apps_data/radiogeddon/fingerprints.tmp")
#define RADIOGEDDON_FP_MARK  EXT_PATH("apps_data/radiogeddon/fingerprints.cmt")

/* Matches shown are at least this similar. */
#define RADIOGEDDON_FP_MIN_SCORE 50u

typedef struct {
    char name[RG_FP_NAME_MAX];
    RgFpMatch match;
} RadioGeddonFpHit;

typedef struct {
    bool target_found; // the recording searched for has a fingerprint
    bool target_structure; // ... with decodable structure
    bool target_raw;
    uint16_t target_bits;
    uint16_t target_te;
    uint8_t target_encoding;
    uint8_t count; // hits
    RadioGeddonFpHit hit[RG_FP_TOP];
    uint32_t indexed; // records in the index now
    uint32_t computed; // fingerprints computed by this search
    uint32_t dropped; // records dropped (file gone or changed, damaged)
    uint32_t unreadable; // recordings that could not be fingerprinted
    uint32_t compared; // recordings ranked against the target
    uint32_t limit; // records the index could hold in this search (RG_FP_HAS_LIMIT)
    bool truncated; // more recordings than that (RG_FP_INDEX_MAX or the heap)
    bool index_failed; // the index could not be written (results from memory)
} RadioGeddonFpResult;

#define RG_FP_HAS_LIMIT 1

/** Heap a search needs besides the module (an analysis, name hashes). */
size_t radiogeddon_fp_memory(void);

/**
 * Bring the index up to date and rank every recording against @p target
 * (a path in the signals folder). @p progress is told (done, total) over
 * the files. False only when nothing could be done (no card, no memory).
 */
bool radiogeddon_fp_find_similar(
    Storage* storage,
    const char* target,
    RadioGeddonFpResult* out,
    RadioGeddonProgressCallback progress,
    void* context);

/**
 * Why hit @p index looks similar: the score, its strength, the features
 * that agreed and those that did not, and the reminder that similar
 * structure is not proof of a common transmitter. Returns the length.
 */
size_t radiogeddon_fp_explain(const RadioGeddonFpResult* r, size_t index, char* out, size_t size);

#endif
