/**
 * Synthetic RAW timing for host tests of the analysis tools built on
 * rg_analyzer (Bitstream Explorer, Multi-Capture Compare, Sessions). Every
 * signal is generated in code from known bits, so the expected answer is
 * exact; none of it is evidence about real captures.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SYNTH_TE 350

/* Deterministic pseudo-random source (LCG) for jitter. */
static uint32_t synth_rng_state = 12345u;
static inline uint32_t synth_rng(void) {
    synth_rng_state = synth_rng_state * 1103515245u + 12345u;
    return (synth_rng_state >> 16) & 0x7FFFu;
}

static inline int32_t synth_jitter(int32_t v, int pct) {
    if(!pct) return v;
    return v + (int32_t)((int64_t)v * ((int)(synth_rng() % (2 * pct + 1)) - pct) / 100);
}

/* Princeton-style PWM frame: '0' = Te high / 3Te low, '1' = 3Te / Te, then a
 * Te stop pulse and a 31 Te sync gap. */
static inline void synth_pwm(int32_t* buf, size_t* pos, const char* bits, int jitter_pct) {
    for(const char* b = bits; *b; b++) {
        int32_t hi = (*b == '1') ? 3 * SYNTH_TE : SYNTH_TE;
        int32_t lo = (*b == '1') ? SYNTH_TE : 3 * SYNTH_TE;
        buf[(*pos)++] = synth_jitter(hi, jitter_pct);
        buf[(*pos)++] = -synth_jitter(lo, jitter_pct);
    }
    buf[(*pos)++] = SYNTH_TE;
    buf[(*pos)++] = -31 * SYNTH_TE;
}

/* PPM frame: 500 us pulse, gap 1000 us = '0', 2000 us = '1', 12 ms frame gap. */
static inline void synth_ppm(int32_t* buf, size_t* pos, const char* bits) {
    for(const char* b = bits; *b; b++) {
        buf[(*pos)++] = 500;
        buf[(*pos)++] = (*b == '1') ? -2000 : -1000;
    }
    buf[(*pos)++] = 500;
    buf[(*pos)++] = -12000;
}
