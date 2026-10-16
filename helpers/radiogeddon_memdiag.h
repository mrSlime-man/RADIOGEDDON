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
#include "rg_memstat.h"

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

/* ---- runtime diagnostics (counters only; no capture data) ---------------- */

/** The app's UI tick (GUI responsiveness: gaps between ticks). */
void radiogeddon_memdiag_tick(void);

/** A module load attempt: @p status is a RadioGeddonModuleStatus, @p ms its time. */
void radiogeddon_memdiag_module(const char* file, int status, uint32_t ms);

/** A finished RAW recording (any thread). */
void radiogeddon_memdiag_recording(
    uint32_t samples,
    uint32_t lost,
    uint32_t gaps,
    uint32_t overruns,
    bool error,
    uint32_t bytes,
    uint32_t ms);

/** A message shown to the user (@p header: a static string). */
void radiogeddon_memdiag_message(const char* header);

/** Append the diagnostics (responsiveness, modules, recordings, messages). */
void radiogeddon_memdiag_report_diag(FuriString* out);

/* The counters behind both reports (read by radiogeddon_memdiag_text.c). */
const RgMemStat* radiogeddon_memdiag_stats(void);
const RgDiag* radiogeddon_memdiag_diags(void);
