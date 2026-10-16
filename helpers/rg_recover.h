/**
 * @file rg_recover.h
 * @brief Pure check and repair plan for a RAW recording cut off by a reboot.
 *
 * A capture streams to recording.tmp (rg_rawfmt.h layout) and is renamed when
 * saved. If the Flipper reboots or the battery dies first, the file is left
 * behind with whatever reached the card: the header, then RAW_Data lines up to
 * the writer's last sync, possibly ending inside a line, and possibly followed
 * by bytes that were never written (a cluster allocated but not filled reads
 * back as zeros or old data).
 *
 * rg_recover_feed() streams the file through a small state machine (no line
 * or file in RAM) and finds:
 *  - whether the header says this is a RAW capture, and its frequency;
 *  - how many samples and how much time are readable;
 *  - the byte offset up to which the text is known good. It stops at the
 *    first byte that cannot belong to the layout, and never trusts a final
 *    value without a separator after it: the last one is dropped rather than
 *    kept possibly shortened (a cut "1200" reads as "12").
 *
 * The repair is then: keep the first `cut` bytes, end the open line, append a
 * "# Recovered:" comment (FlipperFormat and rg_raw skip comments; the
 * firmware's file encoder stops at the first non-RAW line, which is the end).
 * Nothing is invented: samples after the cut are not guessed, and the
 * comment says the end may be missing.
 *
 * No SDK headers; tested on the host at every truncation point of a
 * recording, with garbage tails (test/test_recover.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RgRecoverNothing, /* no RAW header: nothing of a capture to keep */
    RgRecoverEmpty, /* a RAW header but no readable sample */
    RgRecoverPartial, /* samples readable; the end was cut off (repair needed) */
    RgRecoverComplete, /* the recorder finished it (cut only after stopping) */
} RgRecoverVerdict;

typedef struct {
    /* lexer state */
    uint32_t offset; /* bytes fed so far */
    uint8_t mode;
    uint8_t digits;
    bool negative;
    uint32_t magnitude;
    char key[40]; /* start of a header or comment line */
    uint8_t key_len;
    bool stopped; /* a byte that cannot belong to the file was found */

    /* findings */
    bool header_raw; /* Filetype: Flipper SubGhz RAW File */
    bool protocol_raw; /* Protocol: RAW */
    uint32_t frequency; /* Hz, 0 unknown */
    uint32_t samples; /* values readable up to `cut` */
    uint64_t duration_us; /* their total duration */
    uint32_t cut; /* keep bytes [0, cut) */
    bool cut_at_line_end; /* the kept text ends with '\n' */
    bool finished; /* a "# Lost:" or "# Recovered:" note: the writer closed it */
} RgRecoverScan;

void rg_recover_init(RgRecoverScan* s);

/** Feed the next @p len bytes of the file. Cheap once stopped. */
void rg_recover_feed(RgRecoverScan* s, const uint8_t* data, size_t len);

/** Call at end of file; returns the verdict. */
RgRecoverVerdict rg_recover_finish(RgRecoverScan* s);

/**
 * The text appended after the first `cut` bytes to finish the file: "\n" if
 * the kept text ends inside a line, then the recovery comment. Returns its
 * length (0 if it does not fit in @p cap with the NUL).
 */
size_t rg_recover_tail(const RgRecoverScan* s, char* out, size_t cap);
