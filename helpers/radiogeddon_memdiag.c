#include "radiogeddon_memdiag.h"
#include "rg_memstat.h"
#include "../radiogeddon_edition.h"

#define TAG "RadioGeddonMem"

static RgMemStat radiogeddon_memdiag_stat;
static RgDiag radiogeddon_memdiag_diag;
static const char* radiogeddon_memdiag_place = "start";

void radiogeddon_memdiag_start(void) {
    rg_memstat_init(&radiogeddon_memdiag_stat, memmgr_get_free_heap());
    memset(&radiogeddon_memdiag_diag, 0, sizeof(radiogeddon_memdiag_diag));
    radiogeddon_memdiag_place = "start";
}

void radiogeddon_memdiag_sample(const char* where) {
    if(where) radiogeddon_memdiag_place = where;
    rg_memstat_sample(
        &radiogeddon_memdiag_stat, memmgr_get_free_heap(), radiogeddon_memdiag_place);
}

uint32_t radiogeddon_memdiag_free(void) {
    return memmgr_get_free_heap();
}

void radiogeddon_memdiag_log(void) {
    const RgMemStat* s = &radiogeddon_memdiag_stat;
    FURI_LOG_I(
        TAG,
        "start %lu free, lowest %lu (%s), peak use %lu, %lu samples",
        (unsigned long)s->start_free,
        (unsigned long)s->lowest_free,
        s->lowest_at,
        (unsigned long)rg_memstat_peak_use(s),
        (unsigned long)s->samples);
}

/* ---- runtime diagnostics ---------------------------------------------------
 * The tick, module loads and messages come from the app thread; recordings
 * can end on the Hopper's thread (auto-recording), so their counters are
 * added atomically. Nothing here holds capture data: no names, frequencies
 * or samples, only counts. */

void radiogeddon_memdiag_tick(void) {
    rg_diag_tick(&radiogeddon_memdiag_diag, furi_get_tick(), radiogeddon_memdiag_place);
}

void radiogeddon_memdiag_module(const char* file, int status, uint32_t ms) {
    RgDiag* d = &radiogeddon_memdiag_diag;
    if(status == 0) {
        d->module_loads++;
        if(ms >= d->module_slowest_ms) {
            d->module_slowest_ms = ms;
            d->module_slowest = file;
        }
    } else if(status == 2) { // RadioGeddonModuleNoMemory
        d->module_refused++;
    } else {
        d->module_failed++;
    }
}

void radiogeddon_memdiag_recording(
    uint32_t samples,
    uint32_t lost,
    uint32_t gaps,
    uint32_t overruns,
    bool error,
    uint32_t bytes,
    uint32_t ms) {
    RgDiag* d = &radiogeddon_memdiag_diag;
    __atomic_fetch_add(&d->recordings, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_samples, samples, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_lost, lost, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_gaps, gaps, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_overruns, overruns, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_errors, error ? 1u : 0u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_bytes, bytes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&d->rec_ms, ms, __ATOMIC_RELAXED);
}

void radiogeddon_memdiag_message(const char* header) {
    radiogeddon_memdiag_diag.messages++;
    radiogeddon_memdiag_diag.last_message = header;
}

const RgMemStat* radiogeddon_memdiag_stats(void) {
    return &radiogeddon_memdiag_stat;
}

const RgDiag* radiogeddon_memdiag_diags(void) {
    return &radiogeddon_memdiag_diag;
}
