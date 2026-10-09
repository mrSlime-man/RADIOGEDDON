/**
 * @file radiogeddon_analysis.h
 * @brief On-device signal analysis, crypto heuristics and comparison.
 *
 * All judgements are explicitly labelled as either CONFIRMED (a firmware
 * protocol decoder matched) or HEURISTIC (a guess from signal statistics).
 * No function here claims cryptographic key recovery.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include "radiogeddon_storage.h"

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

/**
 * Deep structural analysis of an unknown/RAW capture using the signal engine:
 * base Te, encoding hypothesis + confidence, framing, repeated-frame and
 * constant/changing-field inference, best-effort bit extraction and a
 * device-ID candidate. Every inference is labelled [HYPOTHESIS]; nothing is
 * presented as a verified decode and no key is recovered.
 */
void radiogeddon_analysis_unknown(Storage* storage, const char* path, FuriString* out);

/**
 * RAW-vs-RAW timing similarity (0-100) between two files, or -1 if either is
 * not a readable RAW capture. Appends a short explanation line to @p out.
 */
int radiogeddon_analysis_raw_similarity(
    Storage* storage,
    const char* path_a,
    const char* path_b,
    FuriString* out);
