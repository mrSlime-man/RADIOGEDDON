#include "rg_memstat.h"

#include <stddef.h>

#define RG_MEM_COST_SLACK 1024u

void rg_memstat_init(RgMemStat* s, uint32_t free_now) {
    s->start_free = free_now;
    s->lowest_free = free_now;
    s->lowest_at = "start";
    s->samples = 1;
}

void rg_memstat_sample(RgMemStat* s, uint32_t free_now, const char* where) {
    s->samples++;
    if(free_now < s->lowest_free) {
        s->lowest_free = free_now;
        s->lowest_at = where ? where : "?";
    }
}

uint32_t rg_memstat_peak_use(const RgMemStat* s) {
    return s->start_free > s->lowest_free ? s->start_free - s->lowest_free : 0;
}

bool rg_mem_session_fits(uint32_t free_now, uint32_t cost, uint32_t margin) {
    // Never measured: refuse only when even the smallest session could not
    // fit (a session that surely runs the heap out), never otherwise.
    if(cost == 0) return free_now >= RG_MEM_MIN_SESSION_COST;
    return (uint64_t)free_now >= (uint64_t)cost + margin;
}

bool rg_mem_cost_changed(uint32_t stored, uint32_t measured) {
    if(measured == 0) return false;
    if(stored == 0) return true;
    uint32_t diff = stored > measured ? stored - measured : measured - stored;
    return diff > RG_MEM_COST_SLACK;
}

uint32_t rg_mem_session_cost(
    uint32_t free_before,
    uint32_t low_before,
    uint32_t free_after,
    uint32_t low_after) {
    uint32_t cost = free_before > free_after ? free_before - free_after : 0;
    if(low_after < low_before && free_before > low_after && free_before - low_after > cost) {
        cost = free_before - low_after;
    }
    return cost;
}

static uint32_t rg_mem_fnv(uint32_t h, const char* s) {
    if(!s) s = "";
    for(; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

uint32_t rg_mem_firmware_tag(const char* version, const char* githash) {
    uint32_t h = rg_mem_fnv(2166136261u, version);
    h = rg_mem_fnv(h ^ 0xFFu, githash);
    return h ? h : 1u;
}

void rg_diag_tick(RgDiag* d, uint32_t now_ms, const char* where) {
    if(d->ticks++ > 0) {
        uint32_t gap = now_ms - d->last_tick_ms;
        if(gap > d->worst_gap_ms) {
            d->worst_gap_ms = gap;
            d->worst_at = where ? where : "?";
        }
        if(gap > RG_DIAG_STALL_MS) d->stalls++;
    }
    d->last_tick_ms = now_ms;
}

uint8_t rg_mem_fragmentation_pct(uint32_t free_bytes, uint32_t largest_block) {
    if(free_bytes == 0 || largest_block >= free_bytes) return 0;
    return (uint8_t)(100u - (uint32_t)((uint64_t)largest_block * 100u / free_bytes));
}

uint32_t rg_diag_rec_rate(const RgDiag* d) {
    if(d->rec_ms == 0) return 0;
    return (uint32_t)((uint64_t)d->rec_bytes * 1000u / d->rec_ms);
}
