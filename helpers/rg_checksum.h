/**
 * @file rg_checksum.h
 * @brief Checksum / CRC structure hypotheses over the distinct frames of a capture.
 *
 * Given several different frames of the same length (bit strings decoded by
 * rg_analyzer), test whether the last bits of every frame behave like one of
 * the common checksums of the bits before them:
 *
 * - XOR or sum (mod 2^W) of the W-bit words before it, plain or inverted
 *   (one's complement) or negated (two's complement, "sums to zero"), for
 *   W = 8 and W = 4;
 * - CRC-8 (MSB first) over the bytes before it, for six common polynomials
 *   and initial values 0x00 and 0xFF;
 * - a final even or odd parity bit.
 *
 * Words are counted back from the checksum, so leading preamble or sync bits
 * that do not fill a word are left out. The checksum may end up to two bits
 * before the frame's end (a stop or trailer bit).
 *
 * A structure is reported only if it holds on EVERY distinct frame, and only
 * with enough distinct frames for a chance match to be unlikely (see
 * RG_CHECKSUM_MIN_*). Even then it is a hypothesis about structure: nothing
 * is decoded, decrypted or predicted, and a frame that merely repeats proves
 * nothing, so identical frames count once.
 *
 * Pure C with no SDK includes; host-tested (test/test_checksum.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_CHECKSUM_MAX_FRAMES 16u
#define RG_CHECKSUM_MAX_BITS   256u
#define RG_CHECKSUM_MAX_HITS   4u

/* Distinct frames needed before a structure of that width is reported:
 * a chance match has odds of about 2^-(W x frames) per test. */
#define RG_CHECKSUM_MIN_BYTE   3u
#define RG_CHECKSUM_MIN_NIBBLE 5u
#define RG_CHECKSUM_MIN_PARITY 12u

typedef enum {
    RgChecksumXor,
    RgChecksumXorInv, // ~XOR
    RgChecksumSum,
    RgChecksumSumInv, // ~sum
    RgChecksumSumNeg, // -sum (data + checksum sums to zero)
    RgChecksumCrc8,
    RgChecksumParityEven,
    RgChecksumParityOdd,
} RgChecksumKind;

typedef struct {
    RgChecksumKind kind;
    uint8_t width; // checksum width in bits (8, 4 or 1)
    uint8_t poly; // CRC-8 polynomial (CRC only)
    uint8_t init; // CRC-8 initial value (CRC only)
    uint8_t end_skip; // bits after the checksum, before the frame ends
    uint16_t data_bits; // bits the checksum covers (just before it)
    uint16_t frames; // distinct frames it held on (all of them)
} RgChecksumHit;

typedef struct {
    size_t frames; // distinct frames tested
    size_t bit_count; // their length
    RgChecksumHit hit[RG_CHECKSUM_MAX_HITS];
    size_t hit_count;
    bool enough_for_byte; // frames >= RG_CHECKSUM_MIN_BYTE
} RgChecksumResult;

/**
 * Collect frames for testing: @p bits is a '0'/'1' string of @p n bits.
 * Frames of a different length than the first one added, and repeats of a
 * frame already added, are ignored. Returns false when ignored.
 */
typedef struct {
    size_t count;
    size_t bit_count;
    uint8_t frame[RG_CHECKSUM_MAX_FRAMES][RG_CHECKSUM_MAX_BITS / 8];
} RgChecksumSet;

void rg_checksum_set_init(RgChecksumSet* set);
bool rg_checksum_set_add(RgChecksumSet* set, const char* bits, size_t n);

/** Run every test over the collected frames. */
void rg_checksum_analyze(const RgChecksumSet* set, RgChecksumResult* out);

/** CRC-8, MSB first, no reflection, no final XOR (exposed for tests). */
uint8_t rg_checksum_crc8(const uint8_t* data, size_t len, uint8_t poly, uint8_t init);

/** One line describing @p hit, e.g. "last 8 bits = XOR of the 4 bytes before". */
void rg_checksum_describe(const RgChecksumHit* hit, char* out, size_t out_size);
