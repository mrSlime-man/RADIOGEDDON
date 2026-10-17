/**
 * @file rg_memstat.h
 * @brief Pure memory bookkeeping for the diagnostics (no firmware headers).
 *
 * The app samples the firmware's free-heap figure at its start, on every UI
 * tick and right after its large allocations; this keeps the lowest value
 * seen and where, so the app's peak use can be shown. It also decides whether
 * a radio session fits, from the cost measured for an earlier session.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t start_free; /* free heap when the app started */
    uint32_t lowest_free; /* lowest free heap any sample saw */
    const char* lowest_at; /* where that sample was taken (static string) */
    uint32_t samples;
} RgMemStat;

void rg_memstat_init(RgMemStat* s, uint32_t free_now);

/** Record a sample taken at @p where (a static string). */
void rg_memstat_sample(RgMemStat* s, uint32_t free_now, const char* where);

/** Most memory in use beyond the start: start_free - lowest_free (0 if none). */
uint32_t rg_memstat_peak_use(const RgMemStat* s);

/* Far less than any receive session takes (the protocol decoders and the
 * keystore alone take more): below this the session cannot fit. */
#define RG_MEM_MIN_SESSION_COST (16u * 1024u)

/**
 * Whether a radio session that cost @p cost bytes before still fits in
 * @p free_now with @p margin bytes to spare. An unmeasured cost (0) is
 * allowed unless less than RG_MEM_MIN_SESSION_COST is free, when it could
 * only run out of memory.
 */
bool rg_mem_session_fits(uint32_t free_now, uint32_t cost, uint32_t margin);

/*
 * What a receive session is made of. Model 1 (beta 13): the receiver holds
 * only the decoders it feeds, so BinRAW's decoder and its two capture
 * buffers are no longer allocated (radiogeddon_decoders.h). Settings files
 * of beta 12 and older carry no model: their cost is model 0, measured with
 * those buffers.
 */
#define RG_MEM_SESSION_MODEL 1u

/**
 * A stored session cost as a bound for a session of the current model.
 * @p stored was measured with @p stored_model; @p dropped is the heap the
 * current model no longer allocates compared with that one (exact, from the
 * firmware source; 0 when nothing was dropped on this firmware). The old
 * session allocated everything the new one does plus @p dropped, so the
 * result is still an upper bound until a session of this model measures its
 * own cost. Never below RG_MEM_MIN_SESSION_COST; 0 (never measured) stays 0.
 */
uint32_t rg_mem_cost_for_model(uint32_t stored, uint32_t stored_model, uint32_t dropped);

/** Whether a new measurement should replace the stored cost (none, or >1 KB off). */
bool rg_mem_cost_changed(uint32_t stored, uint32_t measured);

/**
 * Heap a radio session took while it was set up, from the free heap and the
 * firmware's low-water mark (lowest free heap since boot) read before and
 * after. The steady cost is free_before - free_after; if setup pushed the
 * low-water mark down, its brief peak (free_before - low_after) is larger and
 * is what counts.
 */
uint32_t rg_mem_session_cost(
    uint32_t free_before,
    uint32_t low_before,
    uint32_t free_after,
    uint32_t low_after);

/**
 * A non-zero tag for the running firmware (from its version and git hash), so
 * a cost measured under another firmware is not used.
 */
uint32_t rg_mem_firmware_tag(const char* version, const char* githash);

/* ---- runtime diagnostics (beta 10) ---------------------------------------- */

/* A tick later than this after the previous one is a stall: the app thread
 * was busy (SD-card work, an analysis) and the screen did not respond. */
#define RG_DIAG_STALL_MS 250u

typedef struct {
    /* GUI responsiveness: gaps between the app's 100 ms ticks */
    uint32_t ticks;
    uint32_t last_tick_ms;
    uint32_t worst_gap_ms;
    uint32_t stalls;
    const char* worst_at; /* the step the app was in (static string) */
    /* modules */
    uint32_t module_loads;
    uint32_t module_refused; /* not enough memory, refused before loading */
    uint32_t module_failed; /* missing or rejected by the loader */
    uint32_t module_slowest_ms;
    const char* module_slowest; /* its file name (static string) */
    /* RAW recordings */
    uint32_t recordings;
    uint32_t rec_samples;
    uint32_t rec_lost;
    uint32_t rec_gaps;
    uint32_t rec_overruns;
    uint32_t rec_errors;
    uint32_t rec_bytes;
    uint32_t rec_ms;
    /* messages shown (mostly errors) */
    uint32_t messages;
    const char* last_message; /* static string */
} RgDiag;

/** A tick at @p now_ms while the app was at step @p where. */
void rg_diag_tick(RgDiag* d, uint32_t now_ms, const char* where);

/**
 * Heap fragmentation indicator, percent: how much of the free heap is NOT
 * in its largest block (0: one block; high: many small pieces, so a large
 * allocation fails although enough is free in total).
 */
uint8_t rg_mem_fragmentation_pct(uint32_t free_bytes, uint32_t largest_block);

/** SD write throughput of the recordings so far, bytes per second (0: none). */
uint32_t rg_diag_rec_rate(const RgDiag* d);
