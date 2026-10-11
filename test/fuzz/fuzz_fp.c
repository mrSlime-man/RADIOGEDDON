/**
 * Fuzz target: the fingerprint index (helpers/rg_fp.c) and the research
 * journal file (helpers/rg_journal.c), both read back from the SD card.
 *
 * Input: the first byte picks the target, the rest is the file's bytes.
 *
 * Invariants (abort on violation):
 *  - a decoded index record has a NUL-terminated, non-empty name without
 *    '/', a fingerprint whose shares and enums are in range, and encodes
 *    back to the same bytes (the format round-trips);
 *  - comparing any two decoded fingerprints gives a score of 0..100 and is
 *    symmetric;
 *  - a journal either decodes and re-encodes to the same bytes, or is
 *    refused and left empty with `recovered` set.
 */
#include "../../helpers/rg_fp.h"
#include "../../helpers/rg_journal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

static void check_index(const uint8_t* data, size_t size) {
    if(size >= RG_FP_HEADER_BYTES) {
        int32_t n = rg_fp_header_decode(data);
        REQUIRE(n >= -1 && n <= (int32_t)RG_FP_INDEX_MAX);
    }
    static RgFpRecord prev;
    bool have_prev = false;
    for(size_t off = 0; off + RG_FP_RECORD_BYTES <= size; off += RG_FP_RECORD_BYTES / 3) {
        RgFpRecord r;
        if(!rg_fp_record_decode(data + off, &r)) continue;
        size_t len = strlen(r.name);
        REQUIRE(len > 0 && len < RG_FP_NAME_MAX && !strchr(r.name, '/'));
        REQUIRE(r.fp.encoding < RgEncodingCount && r.fp.const_pct <= 100);
        uint8_t again[RG_FP_RECORD_BYTES];
        REQUIRE(rg_fp_record_encode(&r, again));
        REQUIRE(memcmp(again, data + off, RG_FP_RECORD_BYTES) == 0);
        if(have_prev) {
            RgFpMatch ab, ba;
            rg_fp_compare(&prev.fp, &r.fp, &ab);
            rg_fp_compare(&r.fp, &prev.fp, &ba);
            REQUIRE(ab.score <= 100 && ab.score == ba.score && ab.evidence == ba.evidence);
        }
        prev = r;
        have_prev = true;
    }
    // Any 64 bytes compared with themselves.
    if(size >= RG_FP_BYTES) {
        RgFingerprint f;
        if(rg_fp_decode(data, &f)) {
            RgFpMatch m;
            rg_fp_compare(&f, &f, &m);
            REQUIRE(m.score <= 100);
        }
    }
}

static void check_journal(const uint8_t* data, size_t size) {
    static RgJournal j;
    static uint8_t out[2048];
    REQUIRE(rg_journal_encoded_size() <= sizeof(out));
    if(rg_journal_decode(&j, data, size)) {
        REQUIRE(!j.recovered);
        REQUIRE(rg_journal_encode(&j, out, sizeof(out)) == size);
        REQUIRE(memcmp(out, data, size) == 0);
        REQUIRE(rg_journal_level(rg_journal_xp(&j)) <= RG_JOURNAL_MAX_LEVEL);
        for(size_t i = 0; i < j.recent_count; i++)
            REQUIRE(rg_journal_recent(&j, i) != NULL);
    } else {
        REQUIRE(j.recovered && j.recent_count == 0 && j.counters[0] == 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 1) return 0;
    if(data[0] & 1) {
        check_journal(data + 1, size - 1);
    } else {
        check_index(data + 1, size - 1);
    }
    return 0;
}
