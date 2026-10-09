/**
 * @file rg_analyzer.h
 * @brief Pure, firmware-independent Sub-GHz signal analysis engine.
 *
 * Operates on a RAW timing stream (signed microsecond durations: positive =
 * carrier high, negative = low/gap) and infers structure WITHOUT any claim of
 * decryption or key recovery:
 *   - pulse-width clustering and base time unit (Te) estimation,
 *   - line-encoding hypothesis (PWM / PPM / Manchester) with a confidence score,
 *   - frame segmentation on long inter-frame gaps,
 *   - repeated-frame detection (a press is usually re-sent several times),
 *   - best-effort bit extraction for PWM/OOK frames,
 *   - constant vs. changing bit fields across repeated frames (reveals a fixed
 *     device-ID portion vs. a rolling counter — described, never "cracked").
 *
 * Every derived result carries a confidence (0-100) and is explicitly a
 * HYPOTHESIS. Nothing here is presented as a verified decode; the firmware's
 * own protocol decoders remain the only source of CONFIRMED identification.
 *
 * No furi/SDK includes, so the whole engine is unit-tested on a host compiler
 * (see test/test_analyzer.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RG_ANALYZER_MAX_CLUSTERS 10
#define RG_ANALYZER_MAX_BITS     256
#define RG_ANALYZER_CLUSTER_TOL  25 /* percent grouping tolerance */
/* A low/gap at least this many times Te is treated as an inter-frame break. */
#define RG_ANALYZER_GAP_FACTOR   7

typedef struct {
    uint32_t center; /* representative duration, us */
    uint32_t count; /* samples in this cluster */
    uint64_t sum; /* running sum for mean recompute */
} RgCluster;

typedef enum {
    RgEncodingUnknown = 0,
    RgEncodingPWM, /* OOK: two pulse widths, bit = which is longer */
    RgEncodingPPM, /* fixed pulse, information in the gap length */
    RgEncodingManchester, /* level transition mid-bit; mostly 1x/2x Te */
} RgEncoding;

typedef struct {
    /* Raw statistics */
    size_t sample_count;
    uint32_t min_us;
    uint32_t max_us;

    /* Timing clusters (sorted ascending by center) */
    RgCluster clusters[RG_ANALYZER_MAX_CLUSTERS];
    size_t cluster_count;
    uint32_t te_us; /* estimated base time unit */

    /* Framing */
    size_t frame_count; /* frames separated by long gaps */
    size_t frame_len_samples; /* length of the representative (first full) frame */
    bool frames_repeat; /* >=2 frames of equal length detected */
    size_t repeat_count; /* number of equal-length frames */

    /* Encoding hypothesis */
    RgEncoding encoding;
    int encoding_confidence; /* 0..100 */

    /* Best-effort bit extraction (PWM representative frame) */
    size_t bit_count;
    char bits[RG_ANALYZER_MAX_BITS + 1]; /* '0'/'1', NUL-terminated */

    /* Constant vs changing fields across repeated equal-length frames */
    bool have_field_diff;
    size_t const_bits;
    size_t changing_bits;
    /* '.' = constant across frames, 'X' = changes; aligned to bits[] */
    char field_map[RG_ANALYZER_MAX_BITS + 1];
} RgAnalysis;

/** Add a duration to the nearest cluster (within tolerance) or start a new one. */
void rg_analyzer_cluster_add(RgCluster* clusters, size_t* n, size_t max, uint32_t value);

/** Sort clusters ascending by center (small arrays; insertion sort). */
void rg_analyzer_sort_clusters(RgCluster* clusters, size_t n);

/**
 * Analyze a RAW timing stream. @p samples are signed durations in us
 * (positive = high, negative = low). Results are written to @p out.
 * Safe for empty/degenerate input (fields zeroed, encoding Unknown).
 */
void rg_analyzer_run(const int32_t* samples, size_t count, RgAnalysis* out);

/** Human label for an encoding hypothesis. */
const char* rg_analyzer_encoding_name(RgEncoding e);

/**
 * Similarity (0-100) between two RAW timing streams, tolerant to length and
 * small timing jitter. Used by the signal comparator for RAW captures.
 */
int rg_analyzer_similarity(const int32_t* a, size_t na, const int32_t* b, size_t nb);
