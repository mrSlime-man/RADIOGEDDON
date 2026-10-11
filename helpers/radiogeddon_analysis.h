/**
 * @file radiogeddon_analysis.h
 * @brief On-device signal analysis, crypto heuristics and comparison.
 *
 * Judgements are labelled CONFIRMED (a firmware decoder or the protocol
 * registry matched), HEURISTIC (a guess from signal statistics) or, in the
 * unknown-protocol engine output, OBSERVED (measured from the timing) and
 * HYPOTHESIS (inferred). Plain summary and field lines
 * (name, frequency, the compare =/~ rows) carry no label. No function here
 * recovers keys, decrypts payloads, or predicts rolling codes.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include "radiogeddon_storage.h"
#include "radiogeddon_progress.h"
#include "rg_analyzer.h"
#include "radiogeddon_subghz.h"

/** Append a concise human-readable summary of @p sig to @p out. */
void radiogeddon_analysis_describe(const RadioGeddonLoadedSignal* sig, FuriString* out);

/**
 * Deep signal analysis: pulse-timing clustering for RAW captures, or bit/field
 * structure for decoded protocols. May re-read the file for RAW timing data.
 */
void radiogeddon_analysis_analyze(
    Storage* storage,
    const char* path,
    const RadioGeddonLoadedSignal* sig,
    FuriString* out);

/**
 * Cryptographic characteristics: identifies likely rolling counters / dynamic
 * (encrypted) payloads vs fixed codes, with clear confirmed/heuristic labels.
 */
void radiogeddon_analysis_crypto(const RadioGeddonLoadedSignal* sig, FuriString* out);

/** Compare two loaded signals field-by-field, highlighting constant vs changed. */
void radiogeddon_analysis_compare(
    const RadioGeddonLoadedSignal* a,
    const RadioGeddonLoadedSignal* b,
    FuriString* out);

typedef enum {
    RadioGeddonAnalysisOk,
    RadioGeddonAnalysisNoMemory, /* not enough free heap to run safely */
    RadioGeddonAnalysisOpenFailed,
    RadioGeddonAnalysisNoRaw, /* the file holds no RAW_Data */
    RadioGeddonAnalysisCorrupt, /* malformed RAW values were skipped */
} RadioGeddonAnalysisStatus;

/**
 * Stream a RAW .sub through the three analyzer passes without loading it.
 * Returns the finished analyzer (caller frees) or NULL with @p status saying
 * why. A non-NULL result may still carry RadioGeddonAnalysisCorrupt.
 */
RgAnalyzer* radiogeddon_analysis_run_file(
    Storage* storage,
    const char* path,
    RadioGeddonAnalysisStatus* status);

/** As above on an already open file (its reader keeps the seek checkpoints). */
RgAnalyzer*
    radiogeddon_analysis_run_raw(RadioGeddonRawFile* file, RadioGeddonAnalysisStatus* status);

/**
 * Report the progress of whole-file analyses (the passes of
 * radiogeddon_analysis_run_raw) to @p callback; NULL stops reporting.
 */
void radiogeddon_analysis_set_progress(RadioGeddonProgressCallback callback, void* context);

/** Report progress to the callback set above, if any. */
void radiogeddon_analysis_report_progress(uint32_t done, uint32_t total);

/** Append a short user-facing explanation of a non-Ok @p status. */
void radiogeddon_analysis_cat_status(FuriString* out, RadioGeddonAnalysisStatus status);

/**
 * Whole-file structural analysis of an unknown/RAW capture: OBSERVED timing
 * (peaks, noise, jitter, frames) and HYPOTHESIS structure (encoding with
 * confidence and runner-up, bit length, repeated patterns, frame alignment,
 * constant vs changing fields, ID candidate) plus a per-frame list. Nothing
 * is presented as a verified decode and no key is recovered.
 */
void radiogeddon_analysis_unknown(Storage* storage, const char* path, FuriString* out);

/** radiogeddon_analysis_unknown's shape, for passing it (or a module's copy). */
typedef void (*RadioGeddonUnknownFn)(Storage* storage, const char* path, FuriString* out);

/**
 * Where the Unknown Protocol Analysis comes from when it is needed: begin()
 * returns it (loading it first if it is a module) or NULL when it cannot be
 * had now (not enough memory); end() releases it.
 */
typedef struct {
    RadioGeddonUnknownFn (*begin)(void* context);
    void (*end)(void* context);
    void* context;
} RadioGeddonUnknownProvider;

/**
 * Run the firmware's protocol decoders over a RAW capture (see
 * radiogeddon_subghz_decode_raw) and list what they decoded as CONFIRMED:
 * each distinct decode once, with its repeat count, when it was first and
 * last decoded in the capture, and the decoder's own description. Progress
 * goes to radiogeddon_analysis_set_progress's callback.
 */
void radiogeddon_analysis_decode(
    Storage* storage,
    RadioGeddonSubGhz* subghz,
    const char* path,
    FuriString* out);

/**
 * Compare two RAW captures: streamed sample-by-sample timing similarity
 * (0-100, or -1 if either has no RAW data) plus a comparison of their
 * dominant frame patterns, which does not depend on where recording started.
 */
int radiogeddon_analysis_raw_similarity(
    Storage* storage,
    const char* path_a,
    const char* path_b,
    FuriString* out);
