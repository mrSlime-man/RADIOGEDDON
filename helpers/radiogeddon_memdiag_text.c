#include "radiogeddon_memdiag.h"
#include "rg_memstat.h"
#include "../radiogeddon_edition.h"
#include "radiogeddon_host_map.h"

/*
 * The memory and diagnostics text (About and its saved report), apart from
 * the counters in radiogeddon_memdiag.c: in the Full edition this is linked
 * into the About module and reads the counters through the host table, so
 * only the counting stays resident.
 */

static void radiogeddon_memdiag_kb(FuriString* out, const char* label, uint32_t bytes) {
    furi_string_cat_printf(
        out,
        "%s: %lu.%lu KB\n",
        label,
        (unsigned long)(bytes / 1024),
        (unsigned long)((bytes % 1024) * 10 / 1024));
}

void radiogeddon_memdiag_report(FuriString* out, uint32_t radio_session_cost) {
    const RgMemStat* s = radiogeddon_memdiag_stats();
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

void radiogeddon_memdiag_report_diag(FuriString* out) {
    const RgDiag* d = radiogeddon_memdiag_diags();
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
