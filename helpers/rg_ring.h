/**
 * @file rg_ring.h
 * @brief Lock-free single-producer / single-consumer ring of RAW samples.
 *
 * The radio worker thread pushes signed durations; a writer thread pops them
 * and streams them to the SD card. Neither side ever waits for the other: a
 * push into a full ring is refused at once and counted as lost, so a slow
 * card can cost samples but can never stall the radio. Losses are recorded as
 * gaps (runs of consecutive refused samples) so they can be reported.
 *
 * Head and tail run freely and wrap at 2^32; the capacity is a power of two.
 * Only GCC/Clang __atomic builtins are used, so the file has no SDK includes
 * and is unit-tested on the host, including with a real producer thread
 * (test/test_ring.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int32_t* buf;
    uint32_t mask; /* capacity - 1 */
    uint32_t head; /* next slot to write; written by the producer only */
    uint32_t tail; /* next slot to read; written by the consumer only */

    /* Statistics, written by the producer only. */
    uint32_t pushed; /* samples accepted */
    uint32_t dropped; /* samples refused because the ring was full */
    uint32_t gaps; /* runs of consecutive refused samples */
    uint32_t first_gap; /* samples accepted before the first refusal */
    uint32_t peak; /* highest fill level reached */
    bool in_gap;
} RgRing;

/**
 * Use @p storage (room for @p capacity samples) as the ring. @p capacity must
 * be a power of two of at least 2; returns false otherwise.
 */
bool rg_ring_init(RgRing* r, int32_t* storage, uint32_t capacity);

/** Producer: append one sample. False (and counted as lost) when full. */
bool rg_ring_push(RgRing* r, int32_t value);

/** Consumer: move up to @p max samples into @p out, oldest first. */
size_t rg_ring_pop(RgRing* r, int32_t* out, size_t max);

/** A consistent-enough snapshot of the statistics, for display. */
typedef struct {
    uint32_t pushed;
    uint32_t dropped;
    uint32_t gaps;
    uint32_t first_gap;
    uint32_t peak;
    uint32_t used;
} RgRingStats;

/** Read the statistics from any thread. */
void rg_ring_stats(const RgRing* r, RgRingStats* out);

/** Samples waiting to be popped (safe from either side). */
uint32_t rg_ring_used(const RgRing* r);

uint32_t rg_ring_capacity(const RgRing* r);

/**
 * Largest power-of-two capacity whose storage fits in @p bytes, capped at
 * @p max_cap; 0 if even @p min_cap does not fit.
 */
uint32_t rg_ring_capacity_for(size_t bytes, uint32_t min_cap, uint32_t max_cap);

/** Signed RAW sample for a level and duration, clamped to the int32 range. */
int32_t rg_ring_sample(bool level, uint32_t duration_us);
