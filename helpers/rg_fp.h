/**
 * @file rg_fp.h
 * @brief Signal Fingerprint (Full edition): compact structural summaries of
 * saved recordings, their similarity and their index on the SD card.
 *
 * A fingerprint describes how a recording is built, not what it says:
 * frequency and preset, the base timing Te, the shapes of the pulse and gap
 * distributions in units of Te (quantised, so receiver jitter of a few
 * percent lands in the same bucket), the frame length and encoding the
 * analyzer inferred, the preamble and the sync gap before the data, how
 * frames repeat, and hashes of the dominant frame and of its constant bits.
 * Nothing depends on where the recording started or how many times a frame
 * was repeated: the distributions are shares, not counts or positions.
 *
 * Similarity compares two fingerprints feature by feature. A feature that
 * one of them lacks (no frame decoded, no repeat seen) is left out rather
 * than counted as a match or a mismatch, and the evidence that matched is
 * returned as flags so the screen can say WHY two recordings look alike.
 * Similar structure is never proof of a common transmitter: recordings
 * without decodable structure (noise, a carrier) are capped low whatever
 * else agrees.
 *
 * The index is a file of fixed-size records (name, file size, timestamp,
 * fingerprint, CRC-32). A record whose CRC fails is skipped without losing
 * the place of the next one; the header carries a version, so a fingerprint
 * of another layout is rebuilt, not misread.
 *
 * Pure C with no SDK headers; host-tested in test/test_fp.c and fuzzed
 * (test/fuzz/fuzz_fp.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rg_analyzer.h"

/* Timing buckets (Te multiples): <0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 17, 28, >40. */
#define RG_FP_BINS 12

/* RgFingerprint.flags */
#define RG_FP_FLAG_RAW       0x01u /* from RAW timing (else a decoded file's fields) */
#define RG_FP_FLAG_STRUCTURE 0x02u /* frames decoded under an encoding (fit >= 60) */
#define RG_FP_FLAG_TRUNCATED 0x04u /* the longest frames were cut by the analyzer */
#define RG_FP_FLAG_CHANGING  0x08u /* some bits change between frames */

typedef struct {
    uint8_t flags;
    uint8_t preset; /* preset id (caller's numbering), 0xFF unknown */
    uint8_t encoding; /* RgEncoding */
    uint8_t alternative; /* runner-up RgEncoding */
    uint8_t quality; /* RgQuality */
    uint8_t noise_pct;
    uint8_t jitter_pct;
    uint8_t preamble; /* leading square-wave cells before the data (median, saturating) */
    uint8_t sync_ratio; /* first long gap after the preamble, in Te (0: none) */
    uint8_t const_pct; /* share of compared bits that never changed */
    uint8_t groups; /* distinct frame patterns */
    uint8_t fit; /* decode fit of the signal frames, 0..100 */
    uint32_t frequency; /* Hz, 0 unknown */
    uint16_t te_us;
    uint16_t bit_count; /* most common frame length in bits */
    uint16_t frames; /* frames cut on gaps (saturating) */
    uint16_t repeat_ms; /* median start-to-start interval of repeats, 0 none */
    uint8_t high_hist[RG_FP_BINS]; /* share (percent) of high pulses per bucket */
    uint8_t low_hist[RG_FP_BINS]; /* share of in-frame gaps per bucket */
    uint32_t pattern_hash; /* the dominant frame's bits (0: none) */
    uint32_t stable_hash; /* values of the bits that never changed (0: none) */
    uint32_t proto_hash; /* decoded files: protocol name (0: RAW) */
} RgFingerprint;

/* Serialized size of a fingerprint (little-endian, fixed layout). */
#define RG_FP_BYTES 64

/* Evidence flags: features that agreed (each scored >= 70 of 100). */
typedef enum {
    RgFpEvFrequency = 1u << 0,
    RgFpEvPreset = 1u << 1,
    RgFpEvTe = 1u << 2,
    RgFpEvTiming = 1u << 3, /* pulse and gap distributions */
    RgFpEvEncoding = 1u << 4,
    RgFpEvFrameLength = 1u << 5,
    RgFpEvPreamble = 1u << 6,
    RgFpEvSync = 1u << 7,
    RgFpEvRepeat = 1u << 8,
    RgFpEvPattern = 1u << 9, /* identical dominant frame */
    RgFpEvStable = 1u << 10, /* the same constant bits */
    RgFpEvProtocol = 1u << 11, /* decoded files: the same protocol */
} RgFpEvidence;
#define RG_FP_EVIDENCE_COUNT 12

/* Score cap when either recording has no decodable structure. */
#define RG_FP_NO_STRUCTURE_CAP 30

typedef struct {
    uint8_t score; /* 0..100 */
    uint16_t evidence; /* RgFpEvidence flags that agreed */
    uint16_t contradicts; /* features compared that disagreed (scored < 30) */
} RgFpMatch;

/* ---- building ---------------------------------------------------------------- */

/**
 * The 4th, cheap pass: streamed over the same samples the analyzer read, it
 * measures the timing distributions, the preamble and the sync gap in units
 * of the analysis' Te. Memory is this struct (~120 bytes).
 */
typedef struct {
    uint32_t te;
    uint32_t gap_us;
    uint32_t high[RG_FP_BINS];
    uint32_t low[RG_FP_BINS];
    /* the frame being read */
    uint32_t cells; /* square-wave cells at its start */
    uint32_t pos; /* samples into the frame */
    bool in_preamble;
    bool sync_done;
    int32_t pending; /* merging same-level samples */
    /* per-frame results */
    uint8_t preambles[16];
    uint8_t syncs[16];
    uint8_t frames;
} RgFpBuilder;

void rg_fp_builder_init(RgFpBuilder* b, const RgAnalysis* analysis);
void rg_fp_builder_feed(RgFpBuilder* b, const int32_t* samples, size_t count);

/**
 * Finish a RAW fingerprint from the analysis and the 4th pass (@p b may be
 * NULL: no distributions, preamble or sync). @p frequency in Hz and @p
 * preset as the caller numbers presets (0xFF unknown).
 */
void rg_fp_from_analysis(
    RgFingerprint* fp,
    const RgAnalysis* r,
    RgFpBuilder* b,
    uint32_t frequency,
    uint8_t preset);

/** A decoded file's fingerprint: its protocol, bit count and key. */
void rg_fp_from_decoded(
    RgFingerprint* fp,
    const char* protocol,
    uint16_t bits,
    uint32_t key_hash,
    uint32_t frequency,
    uint8_t preset);

/** Bucket of duration @p us at base time @p te (0..RG_FP_BINS-1). */
size_t rg_fp_bin(uint32_t us, uint32_t te);

/** FNV-1a over @p n bytes, continuing from @p h (start with RG_FP_FNV_INIT). */
#define RG_FP_FNV_INIT 2166136261u
uint32_t rg_fp_fnv(uint32_t h, const void* data, size_t n);

/* ---- similarity -------------------------------------------------------------- */

void rg_fp_compare(const RgFingerprint* a, const RgFingerprint* b, RgFpMatch* out);

/** "Strong", "Similar", "Weak" or "Different" for a score. */
const char* rg_fp_strength(uint8_t score);

/** Short label of one evidence flag ("timing", "frame length", ...). */
const char* rg_fp_evidence_name(RgFpEvidence ev);

/**
 * The evidence as text, comma-separated, at most @p max items (most telling
 * first). Returns the length written (NUL-terminated, cut to @p size).
 */
size_t rg_fp_evidence_text(uint16_t evidence, size_t max, char* out, size_t size);

/* ---- ranking ----------------------------------------------------------------- */

#define RG_FP_TOP 8

typedef struct {
    uint32_t id; /* caller's id of the candidate (an index record number) */
    RgFpMatch match;
} RgFpCandidate;

typedef struct {
    RgFpCandidate c[RG_FP_TOP];
    uint8_t count;
    uint8_t min_score; /* candidates below this are not kept */
} RgFpRank;

void rg_fp_rank_init(RgFpRank* r, uint8_t min_score);
/** Offer a candidate; kept if among the RG_FP_TOP best (ties: first offered). */
void rg_fp_rank_offer(RgFpRank* r, uint32_t id, const RgFpMatch* m);

/* ---- serialization and the index file -------------------------------------- */

void rg_fp_encode(const RgFingerprint* fp, uint8_t out[RG_FP_BYTES]);
/** False when the bytes cannot be a fingerprint (bad enum, share > 100). */
bool rg_fp_decode(const uint8_t in[RG_FP_BYTES], RgFingerprint* fp);

#define RG_FP_INDEX_MAGIC   "RGFI"
#define RG_FP_INDEX_VERSION 1u
#define RG_FP_HEADER_BYTES  16
#define RG_FP_NAME_MAX      64 /* name bytes in a record, NUL padded */
#define RG_FP_RECORD_BYTES  (RG_FP_NAME_MAX + 4 + 4 + RG_FP_BYTES + 4)
/* Records an index holds at most (the Database's own cap). */
#define RG_FP_INDEX_MAX     500u

typedef struct {
    char name[RG_FP_NAME_MAX]; /* file name in the signals folder, NUL-terminated */
    uint32_t size; /* the recording's size when fingerprinted */
    uint32_t timestamp; /* its modification time then (0 if unknown) */
    RgFingerprint fp;
} RgFpRecord;

void rg_fp_header_encode(uint32_t count, uint8_t out[RG_FP_HEADER_BYTES]);
/** The record count, or -1 when the header is not this version's. */
int32_t rg_fp_header_decode(const uint8_t in[RG_FP_HEADER_BYTES]);

/** False if @p name does not fit a record (empty or too long). */
bool rg_fp_record_encode(const RgFpRecord* rec, uint8_t out[RG_FP_RECORD_BYTES]);
/** False when the CRC, the name or the fingerprint is bad (skip the record). */
bool rg_fp_record_decode(const uint8_t in[RG_FP_RECORD_BYTES], RgFpRecord* rec);

/** The record still describes a file of this @p size and @p timestamp. */
bool rg_fp_record_current(const RgFpRecord* rec, uint32_t size, uint32_t timestamp);
