#include "rg_hop.h"

#include <string.h>

void rg_hop_config_default(RgHopConfig* config) {
    config->dwell_ms = 200;
    config->hold_ms = 2000;
    rg_scan_params_default(&config->detect);
}

void rg_hop_init(RgHop* hop, size_t count, uint32_t now_ms) {
    memset(hop, 0, sizeof(*hop));
    if(count > RG_HOP_MAX_CHANNELS) count = RG_HOP_MAX_CHANNELS;
    hop->count = count;
    hop->tuned_ms = now_ms;
    for(size_t i = 0; i < RG_HOP_MAX_CHANNELS; i++) {
        rg_scan_channel_reset(&hop->channels[i]);
    }
}

static RgHopEvent* rg_hop_newest(RgHop* hop) {
    if(hop->event_count == 0) return NULL;
    size_t idx = (hop->event_head + RG_HOP_MAX_EVENTS - 1) % RG_HOP_MAX_EVENTS;
    return &hop->events[idx];
}

static void rg_hop_open_event(RgHop* hop, float rssi, uint32_t now_ms) {
    RgHopEvent* e = &hop->events[hop->event_head];
    memset(e, 0, sizeof(*e));
    e->channel = (uint8_t)hop->current;
    e->open = true;
    e->start_ms = now_ms;
    e->peak_dbm = rssi;
    hop->event_head = (hop->event_head + 1) % RG_HOP_MAX_EVENTS;
    if(hop->event_count < RG_HOP_MAX_EVENTS) hop->event_count++;
}

static bool
    rg_hop_begin_or_extend(RgHop* hop, const RgHopConfig* config, float rssi, uint32_t now_ms) {
    uint32_t until = now_ms + config->hold_ms;
    if(!hop->holding) {
        hop->holding = true;
        hop->hold_started_ms = now_ms;
        hop->hold_until_ms = until;
        rg_hop_open_event(hop, rssi, now_ms);
        return true;
    }
    if((int32_t)(until - hop->hold_until_ms) > 0) hop->hold_until_ms = until;
    return false;
}

static void rg_hop_end_hold(RgHop* hop, uint32_t now_ms) {
    hop->holding = false;
    hop->hold_until_ms = 0;
    hop->stats[hop->current].active_ms += now_ms - hop->hold_started_ms;
    RgHopEvent* e = rg_hop_newest(hop);
    if(e && e->open) {
        e->open = false;
        e->duration_ms = now_ms - e->start_ms;
    }
}

RgHopStep rg_hop_feed(RgHop* hop, const RgHopConfig* config, float rssi, uint32_t now_ms) {
    RgHopStep step = {0};
    if(hop->count == 0) return step;

    RgScanChannel* ch = &hop->channels[hop->current];
    bool started = rg_scan_channel_update(ch, rssi, &config->detect, now_ms);

    if(started || (ch->active && hop->holding)) {
        if(rg_hop_begin_or_extend(hop, config, rssi, now_ms)) step.activity_started = true;
    }

    if(hop->holding) {
        RgHopEvent* e = rg_hop_newest(hop);
        if(e && e->open && rssi > e->peak_dbm) e->peak_dbm = rssi;
        if(!ch->active && (int32_t)(now_ms - hop->hold_until_ms) >= 0) {
            rg_hop_end_hold(hop, now_ms);
            step.activity_ended = true;
            // Give the frequency a fresh dwell before moving on, so the end of
            // a hold and a retune never happen on the same reading.
            hop->tuned_ms = now_ms;
        }
    }

    if(!hop->holding && !hop->locked && hop->count > 1 &&
       now_ms - hop->tuned_ms >= config->dwell_ms) {
        step.retune = true;
        step.next_channel = rg_hop_next_channel(hop);
    }
    return step;
}

void rg_hop_tuned(RgHop* hop, size_t channel, uint32_t now_ms) {
    if(channel >= hop->count) return;
    if(hop->holding) rg_hop_end_hold(hop, now_ms);
    hop->current = channel;
    hop->tuned_ms = now_ms;
}

bool rg_hop_note_decode(
    RgHop* hop,
    const RgHopConfig* config,
    const char* protocol,
    uint32_t now_ms) {
    if(hop->count == 0) return false;
    if(hop->stats[hop->current].decodes < UINT16_MAX) hop->stats[hop->current].decodes++;
    float level = hop->channels[hop->current].last;
    bool started = rg_hop_begin_or_extend(hop, config, level, now_ms);
    RgHopEvent* e = rg_hop_newest(hop);
    if(e && e->open) {
        if(e->decodes < UINT8_MAX) e->decodes++;
        if(e->protocol[0] == '\0' && protocol) {
            strncpy(e->protocol, protocol, RG_HOP_PROTO_LEN - 1);
            e->protocol[RG_HOP_PROTO_LEN - 1] = '\0';
        }
    }
    return started;
}

void rg_hop_set_locked(RgHop* hop, bool locked, uint32_t now_ms) {
    hop->locked = locked;
    if(!locked) hop->tuned_ms = now_ms;
}

size_t rg_hop_next_channel(const RgHop* hop) {
    if(hop->count == 0) return 0;
    return (hop->current + 1) % hop->count;
}

void rg_hop_mark_recorded(RgHop* hop) {
    RgHopEvent* e = rg_hop_newest(hop);
    if(e) e->recorded = true;
}

const RgHopEvent* rg_hop_event(const RgHop* hop, size_t i) {
    if(i >= hop->event_count) return NULL;
    size_t idx = (hop->event_head + RG_HOP_MAX_EVENTS - 1 - i) % RG_HOP_MAX_EVENTS;
    return &hop->events[idx];
}

uint32_t rg_hop_hold_remaining(const RgHop* hop, uint32_t now_ms) {
    if(!hop->holding) return 0;
    int32_t left = (int32_t)(hop->hold_until_ms - now_ms);
    return left > 0 ? (uint32_t)left : 0;
}
