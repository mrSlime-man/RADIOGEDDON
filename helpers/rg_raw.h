/**
 * @file rg_raw.h
 * @brief Bounded-memory streaming reader for RAW_Data in .sub files.
 *
 * Reads the signed microsecond durations of a Flipper "RAW" .sub file a chunk
 * at a time through a small byte buffer, so a recording of any length is
 * processed without holding it (or even one whole text line) in RAM. Lines
 * other than "RAW_Data:" are skipped; a malformed value marks the reader
 * corrupt and the rest of that line is ignored.
 *
 * While reading forward the reader records up to RG_RAW_CHECKPOINTS resume
 * points (byte offset, sample index, time), thinning them as the file grows,
 * so seeking to a time only re-reads the stretch after the nearest point.
 *
 * The byte source is abstract (read + absolute seek), which keeps this file
 * free of SDK includes: the device wraps a Storage stream, the host tests a
 * memory buffer (see test/test_raw.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RG_RAW_BUF         256
#define RG_RAW_CHECKPOINTS 32
/* First checkpoint spacing in samples; doubles each time the table fills. */
#define RG_RAW_CP_STRIDE   256

typedef struct {
    /* Read up to len bytes into buf; return 0 at end of file. */
    size_t (*read)(void* ctx, uint8_t* buf, size_t len);
    /* Move to an absolute byte offset. */
    bool (*seek)(void* ctx, uint32_t offset);
    void* ctx;
} RgRawSource;

typedef enum {
    RgRawLexLineStart = 0, /* matching the "RAW_Data:" key */
    RgRawLexData, /* inside the value list */
    RgRawLexSkip, /* rest of a non-RAW line */
} RgRawLexState;

typedef struct {
    uint32_t offset; /* byte offset to resume at */
    uint32_t index; /* samples before this point */
    uint64_t time_us; /* duration of those samples */
    uint8_t state; /* RgRawLexState to resume in */
} RgRawCheckpoint;

typedef struct {
    RgRawSource src;
    uint8_t buf[RG_RAW_BUF];
    size_t buf_len;
    size_t buf_pos;
    uint32_t buf_offset; /* file offset of buf[0] */

    /* lexer */
    RgRawLexState state;
    uint8_t key_pos;
    bool in_number;
    bool negative;
    uint32_t magnitude;

    uint32_t index; /* samples returned so far */
    uint64_t time_us; /* their total duration */
    bool eof;
    bool corrupt; /* a non-numeric value was found */
    bool any_data; /* at least one RAW_Data line seen */

    RgRawCheckpoint cp[RG_RAW_CHECKPOINTS];
    size_t cp_count;
    uint32_t cp_stride;
    uint32_t cp_next; /* sample index at which to record the next point */
} RgRawReader;

/** Initialise a reader on @p src positioned at the start of the file. */
void rg_raw_reader_init(RgRawReader* r, RgRawSource src);

/** Back to the start of the file (checkpoints are kept). */
bool rg_raw_reader_rewind(RgRawReader* r);

/** Read up to @p max non-zero samples. Returns 0 at end of data. */
size_t rg_raw_reader_read(RgRawReader* r, int32_t* out, size_t max);

/**
 * Reposition at the latest checkpoint whose time is <= @p time_us (or the
 * start of the file). The caller reads forward from there; rg_raw_reader
 * index/time tell where it landed.
 */
bool rg_raw_reader_seek_time(RgRawReader* r, uint64_t time_us);
