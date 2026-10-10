/**
 * @file rg_rawfmt.h
 * @brief Text formatting for streaming RAW .sub files.
 *
 * Produces exactly the layout the firmware's own RAW recorder writes, so
 * files open in the stock Sub-GHz app and replay through its file encoder:
 *
 *     Filetype: Flipper SubGhz RAW File
 *     Version: 1
 *     Frequency: 433920000
 *     Preset: FuriHalSubGhzPresetOok650Async
 *     Protocol: RAW
 *     RAW_Data: 400 -1200 400 ...      (at most `per_line` values a line)
 *
 * Values are formatted into a caller-owned byte buffer a few at a time, so
 * the writer thread never needs a whole line (or snprintf) on its stack.
 *
 * If samples were lost while recording, one comment line goes at the very
 * end ("# Lost: N samples in G gaps ..."). FlipperFormat ignores comments,
 * the firmware file encoder stops at the first non-RAW line (here: the end
 * anyway), and rg_raw.h reads the count back for the analysis report.
 *
 * No SDK includes; unit-tested on the host (test/test_rawfmt.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Values per RAW_Data line, as the firmware writes them. */
#define RG_RAWFMT_LINE_VALUES 512u
/* Longest text one value can add: "RAW_Data:" + " -2147483648" + "\n". */
#define RG_RAWFMT_VALUE_MAX   24u

typedef struct {
    uint32_t per_line; /* values per line */
    uint32_t in_line; /* values already on the open line (0 = none open) */
} RgRawFmt;

void rg_rawfmt_init(RgRawFmt* f, uint32_t per_line);

/**
 * File header for a RAW capture. Returns its length, or 0 if it does not fit
 * in @p cap bytes (including the terminating NUL).
 */
size_t rg_rawfmt_header(char* out, size_t cap, uint32_t frequency, const char* preset);

/**
 * Append values to @p out (holding @p *len bytes of @p cap). Stops early when
 * fewer than RG_RAWFMT_VALUE_MAX bytes are left; returns the number of values
 * consumed and updates @p *len. Zero values carry no duration and are skipped
 * (counted as consumed).
 */
size_t rg_rawfmt_values(
    RgRawFmt* f,
    const int32_t* values,
    size_t count,
    char* out,
    size_t cap,
    size_t* len);

/** Close the open line, if any. Returns false if there is no room for "\n". */
bool rg_rawfmt_end_line(RgRawFmt* f, char* out, size_t cap, size_t* len);

/**
 * Trailing comment for samples lost to buffer overruns. Returns its length
 * (0 when @p lost is 0 or it does not fit).
 */
size_t rg_rawfmt_lost(char* out, size_t cap, uint32_t lost, uint32_t gaps, uint32_t first_gap);
