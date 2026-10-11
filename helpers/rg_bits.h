/**
 * @file rg_bits.h
 * @brief Full edition: Bitstream Explorer maths over the analyzer's frames.
 *
 * The analyzer (rg_analyzer.h) cuts a RAW capture into frames and decodes
 * each one under the encoding it inferred, keeping up to
 * RG_ANALYZER_MAX_FRAMES frames as packed bit strings. This module answers
 * what the Bitstream Explorer shows about them, using only those bits:
 *
 * - a bit and its position, a frame's similarity to the reference frame and
 *   how many frames repeat its pattern;
 * - bytes from any bit offset, with a frame's leading bits before the first
 *   byte and its trailing bits after the last whole byte kept apart, never
 *   padded into a byte;
 * - which positions are constant or change across the comparable frames
 *   (same length, decoded cleanly, not cut off), and '?' wherever fewer than
 *   two frames give evidence;
 * - the value of a contiguous bit range in binary, hex and decimal.
 *
 * Every bit is a HYPOTHESIS: it depends on the analyzer's encoding guess.
 * Nothing here fills in, guesses or corrects a bit the capture does not hold.
 *
 * Pure C with no SDK includes; host-tested (test/test_bits.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_analyzer.h"

/* Diff markers. */
#define RG_BITS_CONST   '.'
#define RG_BITS_CHANGES 'X'
#define RG_BITS_UNKNOWN '?'

/* Longest field whose numeric value is shown (it fits a uint64_t). */
#define RG_BITS_FIELD_MAX_VALUE_BITS 64u

/** Bit @p i of @p frame: 0 or 1, or -1 beyond the frame. */
int rg_bits_bit(const RgFrame* frame, size_t i);

/** The frame decoded cleanly (fit at or above RG_ANALYZER_GOOD_FIT). */
bool rg_bits_usable(const RgFrame* frame);

/**
 * Reference frame: the first frame of the most common pattern, else the first
 * clean frame; r->frames_kept if there is none.
 */
size_t rg_bits_reference(const RgAnalysis* r);

/**
 * Similarity 0..100 of @p b to @p a: matching bits at the best alignment
 * (up to RG_ANALYZER_MAX_SHIFT bits either way) over the longer frame, so a
 * missing tail counts as different. 0 if either frame is empty.
 */
int rg_bits_similarity(const RgFrame* a, const RgFrame* b, int* shift);

/** Frames sharing frame @p i's pattern (exact + shifted), at least 1. */
size_t rg_bits_repeats(const RgAnalysis* r, size_t i);

/** Letter of frame @p i's pattern ('A'..), or '-' when it has none. */
char rg_bits_pattern(const RgAnalysis* r, size_t i);

/**
 * Frames comparable with frame @p sel: clean, the same length, not cut off.
 * Includes @p sel itself when it qualifies.
 */
size_t rg_bits_comparable(const RgAnalysis* r, size_t sel);

/**
 * Marker for each position of frame @p sel (sel's length + NUL into
 * @p markers): RG_BITS_CONST where every comparable frame has the same bit,
 * RG_BITS_CHANGES where they differ, RG_BITS_UNKNOWN everywhere when fewer
 * than two frames are comparable. Counts of each go to the out pointers
 * (any may be NULL). Returns the number of comparable frames.
 */
size_t rg_bits_diff(
    const RgAnalysis* r,
    size_t sel,
    char* markers,
    size_t* constant,
    size_t* changing,
    size_t* unknown);

/**
 * Byte layout of a frame from bit @p align (0..7): @p lead bits before the
 * first byte, whole bytes, and @p tail bits after the last whole byte.
 */
typedef struct {
    size_t lead;
    size_t bytes;
    size_t tail;
} RgBitsBytes;

void rg_bits_bytes(const RgFrame* frame, size_t align, RgBitsBytes* out);

/** Whole byte @p index from bit align + 8 x index; false beyond the last whole byte. */
bool rg_bits_byte(const RgFrame* frame, size_t align, size_t index, uint8_t* value);

typedef enum {
    RgBitsFieldOk,
    RgBitsFieldEmpty, // the frame has no bits
    RgBitsFieldRange, // start after end, or end beyond the frame
} RgBitsFieldStatus;

typedef struct {
    size_t start; // first bit
    size_t end; // last bit (inclusive)
    size_t length;
    bool has_value; // length <= RG_BITS_FIELD_MAX_VALUE_BITS
    uint64_t value; // MSB first, when has_value
} RgBitsField;

/** The bits @p start..@p end (inclusive) of @p frame. */
RgBitsFieldStatus rg_bits_field(const RgFrame* frame, size_t start, size_t end, RgBitsField* out);

/**
 * Field bits as '0'/'1' text; when they do not fit in @p size - 1 characters
 * the text ends in ".." and false is returned (the value is never cut
 * silently).
 */
bool rg_bits_field_binary(const RgFrame* frame, const RgBitsField* field, char* out, size_t size);

/** Hex of a field value: ceil(length / 4) digits, MSB first ("" without a value). */
void rg_bits_field_hex(const RgBitsField* field, char* out, size_t size);
