/**
 * @file rg_analyzer.h
 * @brief Pure, firmware-independent Sub-GHz signal analysis engine.
 *
 * Works on a RAW timing stream (signed microsecond durations: positive =
 * carrier high, negative = low/gap) and infers structure WITHOUT any claim of
 * decryption or key recovery. The engine streams: the caller feeds the whole
 * recording once per pass, so memory stays bounded (sizeof(RgAnalyzer), about
 * 8 KB) however long the capture is.
 *
 *   Pass 1, timing:   log-spaced duration histograms (high and low) and peak
 *                     detection give the timing clusters, the base unit Te,
 *                     the noise share, jitter and the frame-gap threshold.
 *   Pass 2, encoding: frames are cut on long gaps and every frame is
 *                     trial-decoded as PWM, PPM and Manchester. The encoding
 *                     whose grammar fits the signal frames best wins; the
 *                     runner-up is kept so ambiguity can be shown.
 *   Pass 3, frames:   frames are decoded with the chosen encoding and kept as
 *                     packed bit strings (up to RG_ANALYZER_MAX_FRAMES), then
 *                     grouped into identical patterns, aligned when their
 *                     lengths differ, and compared bit by bit for constant
 *                     versus changing fields.
 *
 * Results are split by evidence level. OBSERVED values are direct
 * measurements of the timing stream (sample counts, timing peaks, frame
 * counts, jitter, noise). Everything that depends on the encoding guess (bits,
 * bit length, groups, field map, ID candidate) is a HYPOTHESIS with a
 * confidence score. Only the firmware's protocol decoders produce CONFIRMED
 * identifications; nothing here does.
 *
 * No furi/SDK includes, so the whole engine is unit-tested on a host compiler
 * (see test/test_analyzer.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RG_ANALYZER_MAX_BITS          256
#define RG_ANALYZER_MAX_PEAKS         8
#define RG_ANALYZER_MAX_FRAMES        48
#define RG_ANALYZER_MAX_GROUPS        6
#define RG_ANALYZER_HIST_BINS         96
/* Longest frame decoded, in samples (256 PWM bits plus preamble). */
#define RG_ANALYZER_FRAME_SAMPLES     520
/* A low at least this many times Te (and >= RG_ANALYZER_MIN_GAP_US) ends a frame. */
#define RG_ANALYZER_GAP_FACTOR        7
#define RG_ANALYZER_MIN_GAP_US        1000u
/* Bursts shorter than this are counted as noise, not frames. */
#define RG_ANALYZER_MIN_FRAME_SAMPLES 8
/* Decode fit (percent of symbols matching the grammar) for a frame to count. */
#define RG_ANALYZER_GOOD_FIT          60
/* Frames whose lengths differ are aligned by up to this many bits. */
#define RG_ANALYZER_MAX_SHIFT         4
#define RG_ANALYZER_NO_GROUP          0xFF

typedef enum {
    RgEncodingUnknown = 0,
    RgEncodingPWM, /* bit = which of two high-pulse widths was sent */
    RgEncodingPPM, /* fixed high pulse, bit = which of two gap widths follows */
    RgEncodingManchester, /* one level transition per bit, durations 1 or 2 Te */
    RgEncodingCount,
} RgEncoding;

typedef enum {
    RgQualityUnknown = 0,
    RgQualityGood,
    RgQualityFair,
    RgQualityPoor,
} RgQuality;

/** A timing peak found in the duration histogram. */
typedef struct {
    uint32_t center_us; /* mean duration of the edges in the peak */
    uint32_t count; /* edges in the peak */
    uint8_t width_bins; /* histogram bins spanned (narrow = clean timing) */
} RgPeak;

/** Timing model from pass 1; also what the trial decoders use. */
typedef struct {
    uint32_t te_us;
    uint32_t pwm_short_us; /* two high-pulse widths, 0 when highs are not bimodal */
    uint32_t pwm_long_us;
    uint32_t ppm_high_us; /* dominant high-pulse width */
    uint32_t ppm_short_us; /* two in-frame low widths, 0 when lows are not bimodal */
    uint32_t ppm_long_us;
} RgDecodeParams;

/** One frame kept for comparison. Bits are packed MSB first. */
typedef struct {
    uint64_t start_us; /* time of the frame's first sample from recording start */
    uint32_t start_index; /* sample index of the frame's first sample */
    uint32_t duration_us;
    uint16_t samples;
    uint16_t bit_count;
    uint8_t fit; /* decode fit 0..100 under the chosen encoding */
    uint8_t group; /* index into RgAnalysis.groups, or RG_ANALYZER_NO_GROUP */
    int8_t shift; /* bits this frame is offset from its group's pattern */
    bool truncated; /* longer than RG_ANALYZER_FRAME_SAMPLES; tail not decoded */
    uint8_t bits[RG_ANALYZER_MAX_BITS / 8];
} RgFrame;

/** Frames sharing one bit pattern. */
typedef struct {
    uint8_t frame; /* index of the frame holding the pattern */
    uint16_t bit_count;
    uint16_t exact; /* frames identical to the pattern (including the first) */
    uint16_t aligned; /* frames matching it after a shift (cut-off or offset) */
} RgGroup;

typedef struct {
    /* ---- OBSERVED: direct measurements of the timing stream ---- */
    size_t sample_count; /* after merging consecutive same-level samples */
    uint64_t duration_us;
    uint32_t min_us;
    uint32_t max_us;
    RgPeak high_peaks[RG_ANALYZER_MAX_PEAKS]; /* narrow high-pulse peaks, ascending */
    size_t high_peak_count;
    RgPeak low_peaks[RG_ANALYZER_MAX_PEAKS]; /* narrow low/gap peaks, ascending */
    size_t low_peak_count;
    uint8_t noise_pct; /* edges outside the narrow peaks */
    uint8_t jitter_pct; /* mean deviation of peak edges from their peak centre */
    uint32_t gap_us; /* frame-gap threshold used */
    size_t frame_count; /* frames cut on gaps */
    size_t burst_count; /* bursts too short to be frames */
    size_t frames_kept; /* frames stored in frames[] */
    RgQuality quality;

    /* ---- HYPOTHESIS: depends on the encoding guess ---- */
    RgDecodeParams params; /* params.te_us is the base time unit estimate */
    uint32_t te_us; /* same as params.te_us */
    RgEncoding encoding;
    int encoding_confidence; /* 0..95; never 100, this is not a verified decode */
    RgEncoding alternative; /* runner-up encoding, Unknown if none fits */
    int alternative_fit;
    int fit_pct; /* mean decode fit of the signal frames under encoding */
    size_t signal_frames; /* frames decoding with fit >= RG_ANALYZER_GOOD_FIT */
    size_t bit_count; /* most common frame length in bits */
    size_t bit_count_frames; /* signal frames with that length */
    char bits[RG_ANALYZER_MAX_BITS + 1]; /* dominant pattern, '0'/'1' */
    RgGroup groups[RG_ANALYZER_MAX_GROUPS]; /* largest first */
    size_t group_count;
    size_t ungrouped; /* signal frames beyond RG_ANALYZER_MAX_GROUPS patterns */
    bool have_field_diff;
    size_t compared; /* frames compared against the reference for field_map */
    size_t const_bits;
    size_t changing_bits;
    char field_map[RG_ANALYZER_MAX_BITS + 1]; /* '.' constant, 'X' changes */
    size_t id_start; /* longest constant run when some bits change */
    size_t id_len; /* 0 when there is no ID candidate */

    RgFrame frames[RG_ANALYZER_MAX_FRAMES];
} RgAnalysis;

/** Log-spaced duration histogram used by the timing pass. */
typedef struct {
    uint32_t count[RG_ANALYZER_HIST_BINS];
    uint64_t sum[RG_ANALYZER_HIST_BINS];
    uint32_t total;
} RgHistogram;

/** Streaming analyzer state. Allocate on the heap on the device. */
typedef struct {
    RgAnalysis result;
    int pass; /* 1 timing, 2 encoding, 3 frames, 0 done */
    RgHistogram hist_high;
    RgHistogram hist_low;
    int32_t pending; /* sample being merged with same-level successors */
    uint64_t time_us;
    size_t index;
    int32_t frame[RG_ANALYZER_FRAME_SAMPLES];
    size_t frame_n;
    size_t frame_samples; /* including samples beyond the buffer */
    uint64_t frame_start_us;
    size_t frame_start_index;
    uint64_t frame_duration_us;
    uint32_t enc_fit_sum[RgEncodingCount];
    uint32_t candidates;
    uint32_t chosen_fit_sum;
} RgAnalyzer;

/** Reset @p a and start pass 1. */
void rg_analyzer_begin(RgAnalyzer* a);

/** Feed the next samples of the recording for the current pass. */
void rg_analyzer_feed(RgAnalyzer* a, const int32_t* samples, size_t count);

/**
 * Finish the current pass. Returns true when another pass over the SAME
 * samples, from the beginning, is required; false when the analysis is done
 * and a->result is final.
 */
bool rg_analyzer_next_pass(RgAnalyzer* a);

/** Convenience: run every pass over an in-memory buffer (heap-allocates). */
void rg_analyzer_run(const int32_t* samples, size_t count, RgAnalysis* out);

/**
 * Decode one frame with @p enc using @p params. Writes '0'/'1' bits
 * (NUL-terminated) and the fit: the percentage of symbols that matched the
 * encoding's grammar. Returns the number of bits.
 *
 * Bit conventions: PWM 1 = long high pulse; PPM 1 = long gap; Manchester
 * 1 = low-to-high transition mid-bit (IEEE 802.3; the G.E. Thomas convention
 * inverts every bit). A lone trailing high pulse is treated as a stop/sync
 * pulse, not a bit.
 */
size_t rg_analyzer_decode(
    RgEncoding enc,
    const RgDecodeParams* params,
    const int32_t* samples,
    size_t count,
    char* bits,
    size_t max_bits,
    int* fit_pct);

/**
 * Best alignment of bit string @p b against @p a, shifting by up to
 * @p max_shift bits either way. A positive shift means b starts that many
 * bits into a. Returns the number of matching bits at the best shift and
 * writes the shift and the overlap length.
 */
size_t rg_analyzer_align(
    const char* a,
    size_t na,
    const char* b,
    size_t nb,
    int max_shift,
    int* shift,
    size_t* overlap);

/** Unpack a frame's bits into '0'/'1' characters (NUL-terminated). */
void rg_analyzer_frame_bits(const RgFrame* frame, char* out);

/** Human label for an encoding. */
const char* rg_analyzer_encoding_name(RgEncoding e);

/** Human label for a quality grade. */
const char* rg_analyzer_quality_name(RgQuality q);

/** Streaming RAW-vs-RAW timing similarity (index-aligned, jitter tolerant). */
typedef struct {
    size_t na;
    size_t nb;
    size_t matches;
} RgSimilarity;

void rg_similarity_init(RgSimilarity* s);
/** Compare @p n samples that sit at the same positions in both recordings. */
void rg_similarity_feed(RgSimilarity* s, const int32_t* a, const int32_t* b, size_t n);
/** Account for samples present in only one recording (the longer tail). */
void rg_similarity_tail(RgSimilarity* s, size_t extra_a, size_t extra_b);
/** Similarity 0..100, scored over the longer recording. */
int rg_similarity_score(const RgSimilarity* s);

/** Similarity (0-100) between two in-memory RAW timing streams. */
int rg_analyzer_similarity(const int32_t* a, size_t na, const int32_t* b, size_t nb);
