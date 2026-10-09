/**
 * @file radiogeddon_memdiag.h
 * @brief Lightweight memory diagnostics from the firmware's heap counters.
 *
 * Samples the free heap at app start, on every UI tick (100 ms) and right
 * after the app's large allocations, and keeps the lowest value seen and the
 * step that preceded it. Only the app's own thread calls in (scenes, ticks
 * and the work they run), so no locking is needed. A sample is one read of a
 * firmware counter; nothing is allocated.
 */
#pragma once

#include <furi.h>

/** Start a new record from the current free heap. */
void radiogeddon_memdiag_start(void);

/**
 * Take a sample. @p where names the step (a static string) and becomes the
 * label of later unnamed samples; NULL keeps the last label (UI ticks).
 */
void radiogeddon_memdiag_sample(const char* where);

/** Free heap now, in bytes. */
uint32_t radiogeddon_memdiag_free(void);

/** Append the memory figures as text for the About screen. */
void radiogeddon_memdiag_report(FuriString* out, uint32_t radio_session_cost);

/** Write a one-line summary to the log. */
void radiogeddon_memdiag_log(void);
