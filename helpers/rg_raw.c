#include "rg_raw.h"

#include <string.h>

static const char rg_raw_key[] = "RAW_Data:";
#define RG_RAW_KEY_LEN (sizeof(rg_raw_key) - 1)
static const char rg_raw_lost_key[] = "# Lost:";
#define RG_RAW_LOST_KEY_LEN (sizeof(rg_raw_lost_key) - 1)
#define RG_RAW_MAX_MAG      0x7FFFFFFFu

static void rg_raw_lexer_reset(RgRawReader* r, RgRawLexState state) {
    r->state = state;
    r->key_pos = 0;
    r->in_number = false;
    r->negative = false;
    r->magnitude = 0;
}

void rg_raw_reader_init(RgRawReader* r, RgRawSource src) {
    memset(r, 0, sizeof(*r));
    r->src = src;
    r->cp_stride = RG_RAW_CP_STRIDE;
    r->cp_next = RG_RAW_CP_STRIDE;
    rg_raw_lexer_reset(r, RgRawLexLineStart);
}

static bool rg_raw_reposition(RgRawReader* r, const RgRawCheckpoint* cp) {
    if(!r->src.seek(r->src.ctx, cp->offset)) return false;
    r->buf_len = 0;
    r->buf_pos = 0;
    r->buf_offset = cp->offset;
    r->index = cp->index;
    r->time_us = cp->time_us;
    r->eof = false;
    rg_raw_lexer_reset(r, (RgRawLexState)cp->state);
    return true;
}

bool rg_raw_reader_rewind(RgRawReader* r) {
    RgRawCheckpoint start = {0, 0, 0, RgRawLexLineStart};
    return rg_raw_reposition(r, &start);
}

/* Record a resume point at a clean token boundary, thinning when full. */
static void rg_raw_checkpoint(RgRawReader* r) {
    if(r->index < r->cp_next) return;
    if(r->cp_count > 0 && r->cp[r->cp_count - 1].index >= r->index) return;
    if(r->cp_count == RG_RAW_CHECKPOINTS) {
        size_t kept = 0;
        for(size_t i = 1; i < RG_RAW_CHECKPOINTS; i += 2)
            r->cp[kept++] = r->cp[i];
        r->cp_count = kept;
        r->cp_stride *= 2;
    }
    RgRawCheckpoint* cp = &r->cp[r->cp_count++];
    cp->offset = r->buf_offset + (uint32_t)r->buf_pos;
    cp->index = r->index;
    cp->time_us = r->time_us;
    cp->state = (uint8_t)r->state;
    r->cp_next = r->index + r->cp_stride;
}

/* Finish a pending number; returns true and writes it when non-zero. */
static bool rg_raw_take(RgRawReader* r, int32_t* out) {
    if(!r->in_number) return false;
    r->in_number = false;
    uint32_t mag = r->magnitude;
    bool neg = r->negative;
    r->magnitude = 0;
    r->negative = false;
    if(mag == 0) return false; /* zero durations carry nothing */
    *out = neg ? -(int32_t)mag : (int32_t)mag;
    return true;
}

static void rg_raw_count(RgRawReader* r, int32_t v) {
    r->index++;
    r->time_us += (uint32_t)(v < 0 ? -(int64_t)v : v);
}

size_t rg_raw_reader_read(RgRawReader* r, int32_t* out, size_t max) {
    size_t n = 0;
    while(n < max) {
        if(r->buf_pos == r->buf_len) {
            if(r->eof) break;
            r->buf_offset += (uint32_t)r->buf_len;
            r->buf_len = r->src.read(r->src.ctx, r->buf, RG_RAW_BUF);
            r->buf_pos = 0;
            if(r->buf_len == 0) {
                r->eof = true;
                if(r->state == RgRawLexLost && r->in_number) r->lost = r->magnitude;
                int32_t v;
                if(r->state == RgRawLexData && rg_raw_take(r, &v)) {
                    out[n++] = v;
                    rg_raw_count(r, v);
                }
                break;
            }
        }
        char c = (char)r->buf[r->buf_pos++];

        if(r->state == RgRawLexLineStart) {
            if(c == '\n') {
                r->key_pos = 0;
            } else if(c == '\r') {
                /* ignore */
            } else if(r->key_pos == 0 && c == rg_raw_lost_key[0]) {
                r->state = RgRawLexComment;
                r->key_pos = 1;
            } else if(c == rg_raw_key[r->key_pos]) {
                if(++r->key_pos == RG_RAW_KEY_LEN) {
                    r->state = RgRawLexData;
                    r->any_data = true;
                }
            } else {
                r->state = RgRawLexSkip;
            }
            continue;
        }
        if(r->state == RgRawLexSkip) {
            if(c == '\n') rg_raw_lexer_reset(r, RgRawLexLineStart);
            continue;
        }
        if(r->state == RgRawLexComment) {
            if(c == '\n') {
                rg_raw_lexer_reset(r, RgRawLexLineStart);
            } else if(c != rg_raw_lost_key[r->key_pos]) {
                r->state = RgRawLexSkip;
            } else if(++r->key_pos == RG_RAW_LOST_KEY_LEN) {
                r->state = RgRawLexLost;
                r->magnitude = 0;
                r->in_number = false;
            }
            continue;
        }
        if(r->state == RgRawLexLost) {
            if(c >= '0' && c <= '9') {
                uint32_t d = (uint32_t)(c - '0');
                r->magnitude = r->magnitude > (RG_RAW_MAX_MAG - d) / 10u ? RG_RAW_MAX_MAG :
                                                                           r->magnitude * 10u + d;
                r->in_number = true;
            } else if(c != ' ' || r->in_number) {
                if(r->in_number) r->lost = r->magnitude;
                rg_raw_lexer_reset(r, c == '\n' ? RgRawLexLineStart : RgRawLexSkip);
            }
            continue;
        }

        /* RgRawLexData */
        if(c >= '0' && c <= '9') {
            uint32_t d = (uint32_t)(c - '0');
            r->magnitude = r->magnitude > (RG_RAW_MAX_MAG - d) / 10u ? RG_RAW_MAX_MAG :
                                                                       r->magnitude * 10u + d;
            r->in_number = true;
        } else if(c == '-' && !r->in_number && !r->negative) {
            r->negative = true;
        } else if(c == ' ' || c == '\t' || c == '\r' || c == '\n' || (c == ',' && r->in_number)) {
            /* A comma right after a value separates it too ("1718, -32700"),
             * as the firmware's RAW player reads it. */
            if(r->negative && !r->in_number) { /* a lone '-' */
                r->corrupt = true;
                rg_raw_lexer_reset(r, c == '\n' ? RgRawLexLineStart : RgRawLexSkip);
                continue;
            }
            int32_t v;
            if(rg_raw_take(r, &v)) {
                out[n++] = v;
                rg_raw_count(r, v);
            }
            if(c == '\n') rg_raw_lexer_reset(r, RgRawLexLineStart);
            rg_raw_checkpoint(r);
        } else {
            r->corrupt = true;
            rg_raw_lexer_reset(r, RgRawLexSkip);
        }
    }
    return n;
}

bool rg_raw_reader_seek_time(RgRawReader* r, uint64_t time_us) {
    const RgRawCheckpoint* best = NULL;
    for(size_t i = 0; i < r->cp_count; i++) {
        if(r->cp[i].time_us <= time_us) best = &r->cp[i];
    }
    if(!best) return rg_raw_reader_rewind(r);
    return rg_raw_reposition(r, best);
}
