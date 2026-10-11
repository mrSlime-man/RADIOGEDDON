/**
 * @file rg_studio.h
 * @brief Pulse Studio (Full edition): the pure logic behind the screen.
 *
 * One frame of a RAW capture at a time: its samples (merged as the analyzer
 * merges them), the bits the analyzer's own decoder reads from them under
 * the capture's encoding, and where each bit's symbol starts
 * (rg_analyzer_decode_map). From that the screen can go both ways: from a
 * pulse to the bit it belongs to, and from a bit to its pulses. Pulses that
 * belong to no bit (a preamble, a sync gap, a stop pulse) map to none, and a
 * symbol that did not fit the grammar maps to an uncertain bit; nothing is
 * ever mapped that the decoder did not read.
 *
 * Also: the waveform window (zoom levels in microseconds per column, pan
 * that keeps the cursor on screen), preamble cells, glitch marks (pulses
 * shorter than half the base time), and the frame's timing histogram in
 * half-Te steps. Pure C, no SDK headers; host-tested in test/test_studio.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rg_analyzer.h"

#define RG_STUDIO_MAX_SAMPLES RG_ANALYZER_FRAME_SAMPLES
#define RG_STUDIO_NO_BIT      0xFFFFu
#define RG_STUDIO_ZOOMS       8
/* Histogram: 16 buckets of half a Te (0.5 .. 8 Te; longer in the last). */
#define RG_STUDIO_HIST        16

typedef struct {
    int32_t s[RG_STUDIO_MAX_SAMPLES]; // signed durations, us
    uint16_t n;
    uint16_t nbits;
    char bits[RG_ANALYZER_MAX_BITS + 1];
    uint16_t map[RG_ANALYZER_MAX_BITS]; // rg_analyzer_decode_map entries
    uint32_t te_us;
    uint64_t span_us; // the frame's duration
    uint16_t preamble; // leading single-Te cells (0 when fewer than 8)
    int fit; // decode fit of this frame, 0..100
    uint8_t encoding; // RgEncoding read with
} RgStudioFrame;

/**
 * Take @p n samples (merging consecutive same-level ones, at most
 * RG_STUDIO_MAX_SAMPLES kept) and decode them with @p enc / @p params.
 */
void rg_studio_load(
    RgStudioFrame* f,
    const int32_t* samples,
    size_t n,
    RgEncoding enc,
    const RgDecodeParams* params);

/** The bit sample @p i belongs to (RG_STUDIO_NO_BIT: none); @p unsure set
 * when the decoder flagged its symbol. */
uint16_t rg_studio_bit_of(const RgStudioFrame* f, uint16_t i, bool* unsure);

/** First sample of bit @p bit's symbol, and the sample after its last. */
bool rg_studio_bit_span(const RgStudioFrame* f, uint16_t bit, uint16_t* first, uint16_t* end);

/** Time (us) from the frame's start to the start of sample @p i. */
uint64_t rg_studio_time_at(const RgStudioFrame* f, uint16_t i);

/** The sample covering time @p t (us from the frame's start). */
uint16_t rg_studio_sample_at(const RgStudioFrame* f, uint64_t t);

/** Pulses or gaps shorter than half a Te: receiver glitches. */
bool rg_studio_glitch(const RgStudioFrame* f, uint16_t i);

/** Microseconds per column at zoom level @p z (0 = closest). */
uint32_t rg_studio_zoom_us(uint8_t z);

/** The zoom level that shows the whole frame in @p cols columns. */
uint8_t rg_studio_zoom_fit(const RgStudioFrame* f, uint16_t cols);

/**
 * Pan so sample @p cursor is visible in @p cols columns at zoom @p z,
 * moving the view start (@p start_us) as little as possible.
 */
uint64_t rg_studio_pan(
    const RgStudioFrame* f,
    uint64_t start_us,
    uint8_t z,
    uint16_t cols,
    uint16_t cursor);

/** Highs and lows of the frame in half-Te buckets (counts, saturating). */
void rg_studio_hist(const RgStudioFrame* f, uint8_t high[RG_STUDIO_HIST], uint8_t low[RG_STUDIO_HIST]);

/** Bits that differ from @p ref (ref '0'/'1' string), aligned at offset 0. */
size_t rg_studio_diff(const RgStudioFrame* f, const char* ref, size_t ref_n, uint8_t* diff_mask);
