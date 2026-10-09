#include "rg_scan.h"

#include <stdio.h>

// Floor tracking rates: fall quickly to quieter readings, rise slowly.
#define RG_SCAN_FLOOR_FALL 0.5f
#define RG_SCAN_FLOOR_RISE 0.05f

void rg_scan_params_default(RgScanParams* params) {
    params->threshold_db = 10.0f;
    params->hysteresis_db = 3.0f;
    params->abs_min_dbm = -100.0f;
}

void rg_scan_channel_reset(RgScanChannel* ch) {
    ch->last = RG_SCAN_RSSI_NONE;
    ch->peak = RG_SCAN_RSSI_NONE;
    ch->floor = RG_SCAN_RSSI_NONE;
    ch->samples = 0;
    ch->last_hit_ms = 0;
    ch->hits = 0;
    ch->active = false;
}

void rg_scan_channel_reset_peak(RgScanChannel* ch) {
    ch->peak = ch->last;
    ch->hits = 0;
    ch->last_hit_ms = 0;
}

bool rg_scan_channel_update(
    RgScanChannel* ch,
    float rssi,
    const RgScanParams* params,
    uint32_t now_ms) {
    ch->last = rssi;
    if(ch->samples == 0 || rssi > ch->peak) ch->peak = rssi;

    if(ch->samples < RG_SCAN_WARMUP_SAMPLES) {
        // Warm-up: the floor is the quietest reading seen so far.
        if(ch->samples == 0 || rssi < ch->floor) ch->floor = rssi;
        ch->samples++;
        return false;
    }
    ch->samples++;

    float on_level = ch->floor + params->threshold_db;
    float off_level = on_level - params->hysteresis_db;
    bool started = false;

    if(ch->active) {
        if(rssi < off_level) ch->active = false;
    } else if(rssi >= on_level && rssi >= params->abs_min_dbm) {
        ch->active = true;
        if(ch->hits < UINT16_MAX) ch->hits++;
        ch->last_hit_ms = now_ms;
        started = true;
    }

    if(!ch->active) {
        float rate = (rssi < ch->floor) ? RG_SCAN_FLOOR_FALL : RG_SCAN_FLOOR_RISE;
        ch->floor += (rssi - ch->floor) * rate;
    }
    return started;
}

float rg_scan_global_floor(const RgScanChannel* channels, size_t count) {
    float floors[32];
    size_t n = 0;
    for(size_t i = 0; i < count && n < 32; i++) {
        if(channels[i].samples < RG_SCAN_WARMUP_SAMPLES) continue;
        // Insertion sort while collecting.
        float v = channels[i].floor;
        size_t j = n++;
        while(j > 0 && floors[j - 1] > v) {
            floors[j] = floors[j - 1];
            j--;
        }
        floors[j] = v;
    }
    if(n == 0) return RG_SCAN_RSSI_NONE;
    if(n % 2) return floors[n / 2];
    return (floors[n / 2 - 1] + floors[n / 2]) / 2.0f;
}

bool rg_scan_in_band(uint32_t frequency_hz, uint32_t lo_hz, uint32_t hi_hz) {
    return frequency_hz >= lo_hz && frequency_hz <= hi_hz;
}

uint32_t
    rg_scan_band_mask(const uint32_t* frequencies, size_t count, uint32_t lo_hz, uint32_t hi_hz) {
    uint32_t mask = 0;
    for(size_t i = 0; i < count && i < 32; i++) {
        if(rg_scan_in_band(frequencies[i], lo_hz, hi_hz)) mask |= (1u << i);
    }
    return mask;
}

// Render a dBm value with one decimal without relying on printf %f support.
static int rg_scan_fmt_dbm(char* out, size_t size, float dbm) {
    if(dbm < -999.0f) dbm = -999.0f;
    if(dbm > 999.0f) dbm = 999.0f;
    int tenths = (int)(dbm * 10.0f + (dbm < 0 ? -0.5f : 0.5f));
    const char* sign = (tenths < 0) ? "-" : "";
    if(tenths < 0) tenths = -tenths;
    return snprintf(out, size, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

size_t rg_scan_format_row(char* out, size_t size, uint32_t frequency_hz, const RgScanChannel* ch) {
    char last[16], peak[16], floor_s[16];
    rg_scan_fmt_dbm(last, sizeof(last), ch->last);
    rg_scan_fmt_dbm(peak, sizeof(peak), ch->peak);
    rg_scan_fmt_dbm(floor_s, sizeof(floor_s), ch->floor);
    int n = snprintf(
        out,
        size,
        "%lu,%s,%s,%s,%u",
        (unsigned long)frequency_hz,
        last,
        peak,
        floor_s,
        (unsigned)ch->hits);
    if(n < 0 || (size_t)n >= size) {
        if(size) out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}
