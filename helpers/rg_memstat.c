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
    if(cost == 0) return true;
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
