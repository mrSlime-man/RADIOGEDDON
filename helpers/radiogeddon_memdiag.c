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

static void radiogeddon_memdiag_kb(FuriString* out, const char* label, uint32_t bytes) {
    furi_string_cat_printf(
        out,
        "%s: %lu.%lu KB\n",
        label,
        (unsigned long)(bytes / 1024),
        (unsigned long)((bytes % 1024) * 10 / 1024));
}

void radiogeddon_memdiag_report(FuriString* out, uint32_t radio_session_cost) {
    const RgMemStat* s = &radiogeddon_memdiag_stat;
    furi_string_cat_printf(out, "Memory (heap)\n");
    radiogeddon_memdiag_kb(out, "Free now", memmgr_get_free_heap());
    uint32_t free_now = memmgr_get_free_heap(), largest = memmgr_heap_get_max_free_block();
    radiogeddon_memdiag_kb(out, "Largest block", largest);
    furi_string_cat_printf(
        out, "Fragmented: %u%%\n", (unsigned)rg_mem_fragmentation_pct(free_now, largest));
    radiogeddon_memdiag_kb(out, "At app start", s->start_free);
    radiogeddon_memdiag_kb(out, "Lowest in app", s->lowest_free);
    furi_string_cat_printf(out, " (after: %s)\n", s->lowest_at);
    radiogeddon_memdiag_kb(out, "Peak app use", rg_memstat_peak_use(s));
    radiogeddon_memdiag_kb(out, "Low since boot", memmgr_get_minimum_free_heap());
    if(radio_session_cost) {
        radiogeddon_memdiag_kb(out, "Radio session", radio_session_cost);
    } else {
        furi_string_cat_printf(out, "Radio session: not\n measured yet\n");
    }
    radiogeddon_memdiag_kb(out, "Total heap", memmgr_get_total_heap());
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

void radiogeddon_memdiag_report_diag(FuriString* out) {
    const RgDiag* d = &radiogeddon_memdiag_diag;
    furi_string_cat_printf(
        out,
        "\nResponsiveness\nUI ticks: %lu\nSlowest: %lu ms\n (in: %s)\nStalls >%ums: %lu\n",
        (unsigned long)d->ticks,
        (unsigned long)d->worst_gap_ms,
        d->worst_at ? d->worst_at : "-",
        (unsigned)RG_DIAG_STALL_MS,
        (unsigned long)d->stalls);
#if RG_EDITION_FULL
    furi_string_cat_printf(
        out,
        "\nModules\nLoaded: %lu\nRefused (memory): %lu\nFailed: %lu\nSlowest: %lu ms\n (%s)\n",
        (unsigned long)d->module_loads,
        (unsigned long)d->module_refused,
        (unsigned long)d->module_failed,
        (unsigned long)d->module_slowest_ms,
        d->module_slowest ? d->module_slowest : "-");
#endif
    furi_string_cat_printf(
        out,
        "\nRAW recordings: %lu\nSamples: %lu\nLost: %lu in %lu gaps\nRadio overruns: %lu\n"
        "Write errors: %lu\nSD write: %lu B/s\n",
        (unsigned long)d->recordings,
        (unsigned long)d->rec_samples,
        (unsigned long)d->rec_lost,
        (unsigned long)d->rec_gaps,
        (unsigned long)d->rec_overruns,
        (unsigned long)d->rec_errors,
        (unsigned long)rg_diag_rec_rate(d));
    furi_string_cat_printf(
        out,
        "\nMessages shown: %lu\nLast: %s\n",
        (unsigned long)d->messages,
        d->last_message ? d->last_message : "-");
}

