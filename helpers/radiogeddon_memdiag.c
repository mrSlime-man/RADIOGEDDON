#include "radiogeddon_memdiag.h"
#include "rg_memstat.h"

#define TAG "RadioGeddonMem"

static RgMemStat radiogeddon_memdiag_stat;
static const char* radiogeddon_memdiag_place = "start";

void radiogeddon_memdiag_start(void) {
    rg_memstat_init(&radiogeddon_memdiag_stat, memmgr_get_free_heap());
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
    radiogeddon_memdiag_kb(out, "Largest block", memmgr_heap_get_max_free_block());
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
