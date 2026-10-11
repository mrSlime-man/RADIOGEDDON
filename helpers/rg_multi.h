/**
 * @file rg_multi.h
 * @brief Full edition: Multi-Capture Compare over several saved recordings.
 *
 * Each recording is analysed on its own, one after another (the analyzer
 * streams it; only one file is open at a time), and boiled down to a compact
 * RgMultiCapture: what the file says (frequency, preset), what the timing
 * shows (Te, encoding guess, frame length) and its most common frame
 * patterns as packed bits. Only these summaries, about 200 bytes each, are
 * kept; the comparison runs over them.
 *
 * The report labels every statement:
 *   [OBSERVED]   read from the files or measured from their timing;
 *   [HEURISTIC]  a rule of thumb over those measurements;
 *   [HYPOTHESIS] an interpretation of the inferred bits (fields that look
 *                like an identifier, a button or a counter). Nothing is
 *                verified, decrypted or predicted: a bit range that stays
 *                the same is never called a serial number, only "constant".
 *
 * Pure C with no SDK includes; host-tested (test/test_multi.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_analyzer.h"

#define RG_MULTI_MAX_CAPTURES    8u
/* Distinct frame patterns kept per capture (of its most common length). */
#define RG_MULTI_PATTERNS        4u
#define RG_MULTI_NAME_LEN        32u
#define RG_MULTI_PRESET_LEN      24u
/* Frequencies this close count as the same channel (drift, rounding). */
#define RG_MULTI_FREQ_TOL_HZ     50000u
/* Te values within this share of each other count as the same timing. */
#define RG_MULTI_TE_TOL_PCT      15u
/* A constant run at least this long, while other bits change, is reported. */
#define RG_MULTI_MIN_ID_BITS     8u
/* Changing runs at most this long with few values look like a button field. */
#define RG_MULTI_MAX_BUTTON_BITS 8u

typedef enum {
    RgMultiSourceNone, // not analysed, or unreadable
    RgMultiSourceRaw, // RAW timing through the analyzer (bits are a HYPOTHESIS)
    RgMultiSourceDecoded, // a key file: bits from a firmware decoder (CONFIRMED)
} RgMultiSource;

typedef struct {
    uint8_t bits[RG_ANALYZER_MAX_BITS / 8];
    uint16_t count; // frames with exactly this pattern in the capture
} RgMultiPattern;

/** What one recording contributes to the comparison. */
typedef struct {
    char name[RG_MULTI_NAME_LEN];
    char preset[RG_MULTI_PRESET_LEN];
    char protocol[RG_MULTI_PRESET_LEN]; // decoded files: the decoder's name
    uint32_t frequency;
    RgMultiSource source;
    RgEncoding encoding;
    int8_t confidence;
    uint8_t noise_pct;
    uint32_t te_us;
    uint16_t frames; // frames cut on gaps
    uint16_t signal_frames; // frames that decoded cleanly
    uint16_t bit_count; // most common frame length (0: no frame)
    uint8_t patterns; // distinct patterns of that length kept
    RgMultiPattern pattern[RG_MULTI_PATTERNS]; // most frequent first
} RgMultiCapture;

/** Reset @p cap and set its name. */
void rg_multi_capture_init(RgMultiCapture* cap, const char* name);

/**
 * Fill @p cap from a finished analysis of a RAW capture: timing, encoding,
 * frame length and the most frequent distinct patterns of that length.
 */
void rg_multi_capture_from_analysis(RgMultiCapture* cap, const RgAnalysis* r);

/** Fill @p cap from a decoded key file: one frame of @p bit_count bits (<= 64) of @p key. */
void rg_multi_capture_from_key(
    RgMultiCapture* cap,
    const char* protocol,
    uint32_t bit_count,
    uint64_t key);

/** Bit @p i of the capture's pattern @p p (0/1, -1 beyond). */
int rg_multi_bit(const RgMultiCapture* cap, size_t p, size_t i);

/** A bounded text buffer the report is written into. */
typedef struct {
    char* buf;
    size_t size;
    size_t len;
    bool truncated;
} RgText;

void rg_text_init(RgText* t, char* buf, size_t size);
void rg_text_printf(RgText* t, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/** A run of bit positions [start, start + length) and what it looks like. */
typedef enum {
    RgMultiRunConstant, // the same in every compared capture
    RgMultiRunButton, // changes, few distinct values (HYPOTHESIS)
    RgMultiRunCounter, // changes, values ordered like a counter (HYPOTHESIS)
    RgMultiRunVaries, // changes, no simple rule
} RgMultiRunKind;

typedef struct {
    uint16_t start;
    uint16_t length;
    uint8_t kind; // RgMultiRunKind
    uint8_t values; // distinct values across the compared captures
} RgMultiRun;

#define RG_MULTI_MAX_RUNS 24u

/** The comparison of the captures' dominant frames. */
typedef struct {
    size_t captures; // analysed
    size_t usable; // with at least one frame
    bool same_frequency;
    bool same_preset;
    bool same_encoding;
    bool same_te;
    bool same_length;
    uint16_t length; // the most common frame length among captures
    size_t compared; // captures whose dominant frame has that length
    uint8_t compared_index[RG_MULTI_MAX_CAPTURES];
    size_t const_bits;
    size_t changing_bits;
    RgMultiRun run[RG_MULTI_MAX_RUNS];
    size_t runs;
    bool runs_truncated;
    int similarity; // mean pairwise similarity of the dominant frames, 0..100
    // Captures whose dominant frames are identical share a label 'A'...
    char same_as[RG_MULTI_MAX_CAPTURES];
    size_t distinct_frames;
    // Captures whose dominant frame was seen only once (no repeat backs it).
    size_t single_frame_captures;
} RgMultiResult;

/** Compare @p count captures (at most RG_MULTI_MAX_CAPTURES). */
void rg_multi_compare(const RgMultiCapture* caps, size_t count, RgMultiResult* out);

/** Write the comparison report (labelled lines, ~24 characters wide). */
void rg_multi_report(
    const RgMultiCapture* caps,
    size_t count,
    const RgMultiResult* res,
    RgText* out);
