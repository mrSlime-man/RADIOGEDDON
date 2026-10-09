/**
 * @file radiogeddon_recorder.h
 * @brief Streaming RAW recorder: bounded RAM, SD writes on their own thread.
 *
 * The radio worker thread pushes each (level, duration) pair into a lock-free
 * ring (rg_ring.h) and returns at once; it never touches the filesystem and
 * never waits. A writer thread drains the ring into RAW_Data lines
 * (rg_rawfmt.h) through a small write buffer. RAM use is fixed when the
 * recording starts (ring sized to the free heap, 4 to 32 KB, plus a 2 KB write
 * buffer and the thread stack), so a recording's length is limited only by
 * the SD card.
 *
 * If the card falls behind for longer than the ring can absorb, new samples
 * are dropped and counted (lost, gaps); the count is shown live and written
 * as a trailing comment in the file. A write error stops writing and is
 * reported through the stats; the file is then incomplete.
 *
 * Lifecycle: alloc (capture can start: push is valid immediately), open
 * (creates the file and starts the writer), push... from the radio thread,
 * then, once the caller guarantees no push is running or will run, finish.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>

typedef struct RadioGeddonRecorder RadioGeddonRecorder;

typedef enum {
    RadioGeddonRecordOk,
    RadioGeddonRecordNoMemory, /* not enough free heap for even a small buffer */
    RadioGeddonRecordOpenFailed, /* the file could not be created */
    RadioGeddonRecordWriteFailed, /* the card refused a write */
} RadioGeddonRecordError;

typedef struct {
    uint32_t samples; /* accepted from the radio */
    uint32_t lost; /* dropped because the buffer was full */
    uint32_t gaps; /* runs of dropped samples */
    uint32_t first_gap; /* samples recorded before the first gap */
    uint32_t radio_overruns; /* the radio worker's own buffer overflowed */
    uint32_t elapsed_ms; /* recording time so far (or in total once stopped) */
    uint32_t buffer; /* ring capacity in samples */
    uint8_t fill_pct; /* ring fill now */
    uint8_t peak_pct; /* highest ring fill seen */
    RadioGeddonRecordError error;
} RadioGeddonRecordStats;

/**
 * Allocate a recorder whose ring fits in the free heap. NULL (with
 * RadioGeddonRecordNoMemory) if even the smallest ring does not fit.
 */
RadioGeddonRecorder* radiogeddon_recorder_alloc(void);

/**
 * Create @p path (replacing any old file), write the RAW header and start the
 * writer thread. Samples pushed before this are kept.
 */
RadioGeddonRecordError radiogeddon_recorder_open(
    RadioGeddonRecorder* rec,
    Storage* storage,
    const char* path,
    uint32_t frequency,
    const char* preset);

/** Radio thread: record one pair. Never blocks; drops it if the ring is full. */
void radiogeddon_recorder_push(RadioGeddonRecorder* rec, bool level, uint32_t duration);

/** Radio thread: the worker reported that its own buffer overflowed. */
void radiogeddon_recorder_note_overrun(RadioGeddonRecorder* rec);

/** Live statistics, from any thread. */
void radiogeddon_recorder_stats(RadioGeddonRecorder* rec, RadioGeddonRecordStats* out);

/** True once a write has failed (the recording should be stopped). */
bool radiogeddon_recorder_failed(RadioGeddonRecorder* rec);

/**
 * Write out everything still buffered, close the file and free the recorder.
 * The caller must ensure no push is running or will run. Blocks while the
 * remaining samples are written. Fills @p out with the final statistics.
 */
void radiogeddon_recorder_finish(RadioGeddonRecorder* rec, RadioGeddonRecordStats* out);

/** Short text for an error, for the UI. */
const char* radiogeddon_recorder_error_text(RadioGeddonRecordError error);
