#include "rg_adapt.h"
#include "rg_div.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_ADAPTIVE_SCAN

#include <string.h>

void rg_adapt_init(RgAdapt* a, RgAdaptMode mode, size_t count, uint16_t dwell_ms) {
    memset(a, 0, sizeof(*a));
    if(count > RG_ADAPT_MAX_CHANNELS) count = RG_ADAPT_MAX_CHANNELS;
    if(mode >= RgAdaptModeCount) mode = RgAdaptStandard;
    if(dwell_ms == 0) dwell_ms = 1;
    a->mode = (uint8_t)mode;
    a->count = (uint8_t)count;
    a->base_dwell_ms = dwell_ms;
    a->min_dwell_ms = dwell_ms;
    // Focused listens up to three times as long on an active frequency.
    uint32_t max = (uint32_t)dwell_ms * (mode == RgAdaptFocused ? 3u : 2u);
    a->max_dwell_ms = (uint16_t)(max > 2000u ? 2000u : max);
    if(a->max_dwell_ms < a->min_dwell_ms) a->max_dwell_ms = a->min_dwell_ms;
    // Every frequency is visited at least once per this many picks.
    uint32_t revisit = count * (mode == RgAdaptFocused ? 4u : 2u);
    a->max_revisit = (uint16_t)(revisit < 2u ? 2u : revisit);
    rg_adapt_reset(a);
}

void rg_adapt_reset(RgAdapt* a) {
    for(size_t i = 0; i < RG_ADAPT_MAX_CHANNELS; i++) {
        uint8_t prio = a->ch[i].priority;
        memset(&a->ch[i], 0, sizeof(a->ch[i]));
        a->ch[i].priority = prio;
        a->ch[i].peak_dbm = -127;
    }
    memset(a->pass, 0, sizeof(a->pass));
    a->picks = 0;
    a->run = 0;
    a->last = 0xFF;
}

void rg_adapt_set_priority(RgAdapt* a, size_t index, uint8_t priority) {
    if(index < a->count) a->ch[index].priority = priority > 3 ? 3 : priority;
}

bool rg_adapt_noisy(const RgAdaptChannel* ch) {
    return ch->visits >= RG_ADAPT_NOISY_VISITS && ch->active_visits * 5u > ch->visits * 4u;
}

/* How strongly a frequency is favoured: 16 for no activity, up to five
 * times that (Balanced) or thirteen times (Focused) for steady activity. */
static uint32_t rg_adapt_weight(const RgAdapt* a, size_t i) {
    const RgAdaptChannel* c = &a->ch[i];
    uint32_t act = c->score;
    if(rg_adapt_noisy(c) && act > RG_ADAPT_NOISY_CAP) act = RG_ADAPT_NOISY_CAP;
    uint32_t focus = a->mode == RgAdaptFocused ? 3u : 1u;
    uint32_t w = 16u + act * focus / 8u;
    if(a->mode == RgAdaptCustom) w += c->priority * 16u;
    return w;
}

/* Picks since channel @p i was last visited (never: as if from the start). */
static uint32_t rg_adapt_wait(const RgAdapt* a, size_t i) {
    const RgAdaptChannel* c = &a->ch[i];
    return c->visited ? a->picks - c->last_pick : a->picks + a->max_revisit;
}

size_t rg_adapt_next(RgAdapt* a) {
    if(a->count == 0) return 0;
    size_t pick = 0;
    if(a->mode == RgAdaptStandard) {
        pick = a->last == 0xFF ? 0 : (a->last + 1u) % a->count;
    } else {
        // 1. Fairness: the most overdue frequency, if any is due.
        uint32_t worst = 0;
        bool due = false;
        for(size_t i = 0; i < a->count; i++) {
            uint32_t w = rg_adapt_wait(a, i);
            if(w >= a->max_revisit && (!due || w > worst)) {
                worst = w;
                pick = i;
                due = true;
            }
        }
        // 2. Otherwise stride scheduling: each frequency advances its own
        //    pass by RG_ADAPT_STRIDE / weight per visit and the lowest pass
        //    goes next, so visits come at a rate proportional to the weight
        //    and evenly spaced rather than in clumps.
        if(!due) {
            uint32_t best = 0;
            bool any = false;
            for(size_t i = 0; i < a->count; i++) {
                if(a->count > 1 && i == a->last && a->run >= RG_ADAPT_MAX_RUN) continue;
                if(!any || a->pass[i] < best) {
                    best = a->pass[i];
                    pick = i;
                    any = true;
                }
            }
        }
        uint32_t step = RG_ADAPT_STRIDE / rg_adapt_weight(a, pick);
        // A frequency that waited (or was forced) restarts from the front.
        uint32_t floor = a->pass[pick];
        for(size_t i = 0; i < a->count; i++)
            if(a->pass[i] < floor) floor = a->pass[i];
        a->pass[pick] = (a->pass[pick] > floor ? a->pass[pick] : floor) + step;
        // Keep the passes small (only their differences matter).
        if(a->pass[pick] > 0x40000000u) {
            for(size_t i = 0; i < a->count; i++)
                a->pass[i] -= floor < a->pass[i] ? floor : a->pass[i];
        }
    }
    if(pick == a->last) {
        if(a->run < 255) a->run++;
    } else {
        a->run = 1;
        a->last = (uint8_t)pick;
    }
    a->picks++;
    return pick;
}

uint16_t rg_adapt_dwell(const RgAdapt* a, size_t index) {
    if(a->mode == RgAdaptStandard || index >= a->count) return a->base_dwell_ms;
    const RgAdaptChannel* c = &a->ch[index];
    if(rg_adapt_noisy(c)) return a->min_dwell_ms;
    // Longer on frequencies with recent activity, in proportion to it.
    uint32_t extra = (uint32_t)(a->max_dwell_ms - a->min_dwell_ms) * c->score / 256u;
    uint32_t d = a->min_dwell_ms + extra;
    return (uint16_t)(d > a->max_dwell_ms ? a->max_dwell_ms : d);
}

void rg_adapt_observe(
    RgAdapt* a,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms) {
    if(index >= a->count) return;
    RgAdaptChannel* c = &a->ch[index];
    if(c->visited && now_ms - c->last_visit_ms > c->max_gap_ms)
        c->max_gap_ms = now_ms - c->last_visit_ms;
    c->visited = 1;
    c->last_pick = a->picks;
    c->last_visit_ms = now_ms ? now_ms : 1u;
    c->visits++;
    c->dwell_ms_total += dwell_ms;
    if(rssi_dbm > c->peak_dbm) c->peak_dbm = rssi_dbm;
    // Old activity fades: every quiet visit takes 1/64 of the weight, so it
    // remembers the activity rate over the last few dozen visits.
    uint32_t score = active ? c->score : c->score - (c->score + 63u) / 64u;
    if(started) {
        c->hits++;
        score += 64u;
    }
    if(active) {
        c->active_visits++;
        c->last_active_ms = c->last_visit_ms;
        score += 16u;
    }
    c->score = (uint16_t)(score > RG_ADAPT_SCORE_MAX ? RG_ADAPT_SCORE_MAX : score);
}

uint8_t rg_adapt_share(const RgAdapt* a, size_t index) {
    if(index >= a->count) return 0;
    uint64_t total = 0;
    for(size_t i = 0; i < a->count; i++)
        total += a->ch[i].visits;
    return total ? (uint8_t)rg_div_u64((uint64_t)a->ch[index].visits * 100u, total) : 0;
}

uint32_t rg_adapt_worst_gap(const RgAdapt* a, uint32_t now_ms) {
    uint32_t worst = 0;
    for(size_t i = 0; i < a->count; i++) {
        const RgAdaptChannel* c = &a->ch[i];
        uint32_t g = c->max_gap_ms;
        // The wait still running counts too.
        if(c->visited && now_ms - c->last_visit_ms > g) g = now_ms - c->last_visit_ms;
        if(g > worst) worst = g;
    }
    return worst;
}

const char* rg_adapt_mode_name(RgAdaptMode mode) {
    switch(mode) {
    case RgAdaptStandard:
        return "Standard";
    case RgAdaptBalanced:
        return "Balanced";
    case RgAdaptFocused:
        return "Focused";
    case RgAdaptCustom:
        return "Custom";
    default:
        return "?";
    }
}

#endif /* RG_FEATURE_ADAPTIVE_SCAN */
