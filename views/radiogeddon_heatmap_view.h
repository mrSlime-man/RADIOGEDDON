/**
 * @file radiogeddon_heatmap_view.h
 * @brief Full edition: RF Activity Heatmap screen (a saved activity log).
 *
 * Header: the time window of the cursor's column, and what the levels show
 * (bursts or strength). Grid: one row per frequency (lowest on top, scrolled
 * when there are more than fit), time left to right in up to 64 columns of
 * 2 px. A cell is blank when nobody listened (not observed: unknown, not
 * silence), one dot when it was listened to and quiet, and denser for more
 * activity. Footer: the cursor cell's frequency and state with its coverage.
 *
 * Keys: arrows move the cursor (held: repeat), OK cycles bursts / strength /
 * legend, Back leaves. The map is the caller's (rg_heatmap.h) and must
 * outlive the view; exists only while the heatmap is open.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <gui/view.h>
#include "../helpers/rg_heatmap.h"

typedef struct RadioGeddonHeatmapView RadioGeddonHeatmapView;

RadioGeddonHeatmapView* radiogeddon_heatmap_view_alloc(void);
void radiogeddon_heatmap_view_free(RadioGeddonHeatmapView* instance);
View* radiogeddon_heatmap_view_get_view(RadioGeddonHeatmapView* instance);

/** Show @p map (kept by pointer); the cursor starts on the log's last column. */
void radiogeddon_heatmap_view_set_map(RadioGeddonHeatmapView* instance, const RgHeatmap* map);

/** Heap the view takes, for the memory check. */
size_t radiogeddon_heatmap_view_memory(void);

#endif
