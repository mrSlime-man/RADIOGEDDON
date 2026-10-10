/**
 * @file rg_decode.h
 * @brief Feed a RAW capture to protocol decoders and collect what they decode.
 *
 * The firmware's own Sub-GHz decoders take (level, duration) pairs. This
 * streams a RAW .sub through an RgRawReader (rg_raw.h) and hands every sample
 * to a decode function the way the firmware's file player
 * (subghz_file_encoder_worker) does for its `subghz decode_raw` command and
 * its decoder unit tests: a positive value is the line high for that many
 * microseconds, a negative one low, and nothing is added after the last.
 *
 * What the decoders report goes into an RgDecodeLog: the same decode (same
 * protocol, data hash and text) is kept once, with how often and when it was
 * first and last decoded, so a remote's repeated frames read as one entry.
 *
 * No SDK includes: the device passes the firmware's receiver, the host tests
 * a stand-in decoder (test/test_decode.c).
 */
#pragma once

#include "rg_raw.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_DECODE_MAX_HITS 12
#define RG_DECODE_NAME     24
#define RG_DECODE_TEXT     200
/* Samples fed between progress reports. */
#define RG_DECODE_PROGRESS_SAMPLES 1024u

typedef struct {
    char protocol[RG_DECODE_NAME];
    char text[RG_DECODE_TEXT]; /* the decoder's description, '\r' removed */
    bool truncated; /* text was longer than fits */
    uint8_t hash; /* the decoder's hash of the decoded data */
    uint32_t count; /* times decoded */
    uint64_t first_us; /* capture time of the first decode */
    uint64_t last_us; /* and of the last */
} RgDecodeHit;

typedef struct {
    RgDecodeHit hit[RG_DECODE_MAX_HITS];
    size_t hit_count;
    uint32_t decodes; /* every decode, repeats included */
    uint32_t dropped; /* decodes of new entries after the table was full */
    uint32_t samples; /* samples fed so far */
    uint64_t time_us; /* capture time at the start of the sample being fed */
} RgDecodeLog;

/** One (level, duration) pair to the decoders. */
typedef void (*RgDecodeFeed)(void* context, bool level, uint32_t duration);

/** Progress: the reader's byte position in the file. */
typedef void (*RgDecodeProgress)(void* context, uint32_t offset);

void rg_decode_log_init(RgDecodeLog* log);

/**
 * Map one RAW sample to what the decoders take: positive is high for that
 * many microseconds, negative low. False for 0, which is not a sample.
 */
bool rg_decode_level(int32_t sample, bool* level, uint32_t* duration);

/**
 * Record a decode reported while a sample is being fed; it is timed at the
 * start of that sample (log->time_us), which is where the decoded frame
 * ended. NULL @p protocol or @p text count as empty.
 */
void rg_decode_log_add(RgDecodeLog* log, const char* protocol, const char* text, uint8_t hash);

/**
 * Feed the rest of @p reader to @p feed. log->time_us and log->samples stay
 * current, so the decoders' callback can use rg_decode_log_add(). @p progress
 * (may be NULL) is called every RG_DECODE_PROGRESS_SAMPLES samples.
 * Returns the number of samples fed.
 */
uint32_t rg_decode_run(
    RgRawReader* reader,
    RgDecodeLog* log,
    RgDecodeFeed feed,
    void* feed_context,
    RgDecodeProgress progress,
    void* progress_context);
