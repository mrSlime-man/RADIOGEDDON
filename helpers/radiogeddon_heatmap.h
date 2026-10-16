/**
 * @file radiogeddon_heatmap.h
 * @brief Full edition (a module): an RF Activity Logger file as a heatmap.
 *
 * Reads a saved activity log (activity/<base>.csv, then <base>_2.csv ...)
 * into an rg_heatmap grid in one pass, a few hundred bytes at a time; the
 * grid is allocated once the log's frequencies are known, and only if it
 * fits. See rg_heatmap.h for what a cell means.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <furi.h>
#include <storage/storage.h>
#include "rg_heatmap.h"

typedef enum {
    RadioGeddonHeatmapOk,
    RadioGeddonHeatmapNoFile, // the log could not be opened
    RadioGeddonHeatmapNoRows, // no rows (an empty or foreign file)
    RadioGeddonHeatmapNoMemory, // the grid does not fit now
} RadioGeddonHeatmapStatus;

/**
 * Read the activity log @p path (any of its parts: all parts of that log
 * are read, first to last) into @p map, the grid allocated in @p *cells
 * (caller frees) only if @p spare bytes stay free beyond it. One pass over
 * the files, a few hundred bytes at a time (rg_heatmap.h).
 */
RadioGeddonHeatmapStatus radiogeddon_actlog_heatmap(
    Storage* storage,
    const char* path,
    RgHeatmap* map,
    RgHeatCell** cells,
    size_t spare);

#endif
