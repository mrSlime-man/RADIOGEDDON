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
