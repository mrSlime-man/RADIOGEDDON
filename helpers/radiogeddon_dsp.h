/**
 * @file radiogeddon_dsp.h
 * @brief Pure signal-processing helpers with no firmware dependencies.
 *
 * Kept free of furi/SDK includes so the algorithms (RAW timing parsing and
 * pulse-width clustering) can be unit-tested on a host compiler. See
 * test/test_dsp.c.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define RADIOGEDDON_MAX_CLUSTERS 8
#define RADIOGEDDON_CLUSTER_TOL  20 // default grouping tolerance, percent

typedef struct {
    uint32_t center; // representative duration (us)
    uint32_t count; // samples in this cluster
    uint64_t sum; // running sum for mean recompute
} RadioGeddonCluster;

/** Add a duration to the nearest cluster (within tolerance) or start a new one. */
void radiogeddon_dsp_cluster_add(
    RadioGeddonCluster* clusters,
    size_t* n,
    size_t max_clusters,
    uint32_t value,
    uint32_t tol_pct);

/**
 * Parse one RAW_Data line of space-separated signed integers (microseconds).
 * Non-zero absolute values update @p count/@p min_us/@p max_us and, when
 * @p clusters is non-NULL, are fed into the clustering accumulator.
 * @return number of non-zero samples parsed from this line.
 */
size_t radiogeddon_dsp_parse_line(
    const char* line,
    size_t* count,
    uint32_t* min_us,
    uint32_t* max_us,
    RadioGeddonCluster* clusters,
    size_t* cluster_n,
    size_t max_clusters);

/** Sort clusters ascending by center (small arrays; insertion sort). */
void radiogeddon_dsp_sort_clusters(RadioGeddonCluster* clusters, size_t n);

/** Count non-zero and distinct byte values across an 8-byte key. */
void radiogeddon_dsp_key_stats(uint64_t key, int* nonzero, int* distinct);
