#include "rg_recover.h"

#include <stdio.h>
#include <string.h>

enum {
    ModeLineStart, /* reading a line's key, up to ':' */
    ModeHeader, /* a header line's value, up to '\n' */
    ModeData, /* RAW_Data values */
    ModeComment, /* a '#' line */
};

static const char rg_recover_note[] =
    "# Recovered: recording interrupted (reboot or power loss); "
    "anything after this point is missing\n";

void rg_recover_init(RgRecoverScan* s) {
    memset(s, 0, sizeof(*s));
    s->mode = ModeLineStart;
    s->cut_at_line_end = true;
}

static bool rg_recover_printable(uint8_t c) {
    return c >= 0x20 && c < 0x7F;
}

/* The key and the start of the value of the line just ended. */
static void rg_recover_header(RgRecoverScan* s) {
    const char* k = s->key;
    const char* colon = strchr(k, ':');
    if(!colon) return;
    const char* v = colon + 1;
    while(*v == ' ')
        v++;
    size_t klen = (size_t)(colon - k);
    if(klen == 8 && memcmp(k, "Filetype", 8) == 0) {
        s->header_raw = strcmp(v, "Flipper SubGhz RAW File") == 0;
    } else if(klen == 8 && memcmp(k, "Protocol", 8) == 0) {
        s->protocol_raw = strcmp(v, "RAW") == 0;
    } else if(klen == 9 && memcmp(k, "Frequency", 9) == 0) {
        uint32_t f = 0;
        bool ok = *v != '\0';
        for(; *v && ok; v++) {
            ok = *v >= '0' && *v <= '9' && f <= 429496729u;
            if(ok) f = f * 10u + (uint32_t)(*v - '0');
        }
        if(ok) s->frequency = f;
    }
}

static void rg_recover_key_add(RgRecoverScan* s, uint8_t c) {
    if(s->key_len + 1u < sizeof(s->key)) {
        s->key[s->key_len++] = (char)c;
        s->key[s->key_len] = '\0';
    }
}

/* The value being read is complete (a separator follows it). */
static bool rg_recover_value_end(RgRecoverScan* s) {
    if(s->digits == 0) return !s->negative; // "-" alone is not a value
    if(s->magnitude != 0) {
        s->samples++;
        s->duration_us += s->magnitude;
    }
    s->digits = 0;
    s->negative = false;
    s->magnitude = 0;
    return true;
}

static void rg_recover_byte(RgRecoverScan* s, uint8_t c) {
    uint32_t at = s->offset; // offset of this byte
    switch(s->mode) {
    case ModeLineStart:
        if(c == '\n' && s->key_len == 0) { // an empty line
            s->cut = at + 1;
            s->cut_at_line_end = true;
        } else if(c == '\n') {
            s->stopped = true; // a key without ':' is not this file's layout
        } else if(c == '\r') {
            // part of a line ending
        } else if(c == '#' && s->key_len == 0) {
            s->mode = ModeComment;
            rg_recover_key_add(s, c);
        } else if(c == ':' && s->key_len > 0) {
            if(strcmp(s->key, "RAW_Data") == 0) {
                s->mode = ModeData;
            } else {
                rg_recover_key_add(s, c);
                s->mode = ModeHeader;
            }
        } else if(rg_recover_printable(c) && c != ' ') {
            rg_recover_key_add(s, c);
        } else {
            s->stopped = true;
        }
        break;
    case ModeHeader:
    case ModeComment:
        if(c == '\n') {
            if(s->mode == ModeHeader) {
                rg_recover_header(s);
            } else if(
                strncmp(s->key, "# Lost:", 7) == 0 || strncmp(s->key, "# Recovered:", 12) == 0) {
                s->finished = true;
            }
            s->cut = at + 1;
            s->cut_at_line_end = true;
            s->mode = ModeLineStart;
            s->key_len = 0;
            s->key[0] = '\0';
        } else if(c == '\r') {
            // ignored, as FlipperFormat does
        } else if(rg_recover_printable(c)) {
            rg_recover_key_add(s, c);
        } else {
            s->stopped = true;
        }
        break;
    case ModeData:
        if(c >= '0' && c <= '9') {
            uint32_t d = (uint32_t)(c - '0');
            // Durations are int32: anything longer is not this file's text.
            if(s->digits >= 10 || s->magnitude > (2147483647u - d) / 10u) {
                s->stopped = true;
                break;
            }
            s->magnitude = s->magnitude * 10u + d;
            s->digits++;
        } else if(c == '-' && s->digits == 0 && !s->negative) {
            s->negative = true;
        } else if(c == ' ' || c == '\r') {
            bool had = s->digits > 0;
            if(!rg_recover_value_end(s)) {
                s->stopped = true;
            } else if(had) {
                s->cut = at; // keep up to the value, drop the separator
                s->cut_at_line_end = false;
            }
        } else if(c == '\n') {
            if(!rg_recover_value_end(s)) {
                s->stopped = true;
                break;
            }
            s->cut = at + 1;
            s->cut_at_line_end = true;
            s->mode = ModeLineStart;
            s->key_len = 0;
            s->key[0] = '\0';
        } else {
            s->stopped = true;
        }
        break;
    }
}

void rg_recover_feed(RgRecoverScan* s, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len && !s->stopped; i++) {
        rg_recover_byte(s, data[i]);
        if(!s->stopped) s->offset++;
    }
}

RgRecoverVerdict rg_recover_finish(RgRecoverScan* s) {
    // A value without a separator after it may be cut short: not counted
    // (rg_recover_value_end never ran for it), and `cut` is before it.
    if(!s->header_raw || !s->protocol_raw) return RgRecoverNothing;
    if(s->samples == 0) return RgRecoverEmpty;
    if(s->finished && !s->stopped && s->cut == s->offset) return RgRecoverComplete;
    return RgRecoverPartial;
}

size_t rg_recover_tail(const RgRecoverScan* s, char* out, size_t cap) {
    size_t n = s->cut_at_line_end ? 0 : 1;
    size_t need = n + sizeof(rg_recover_note) - 1;
    if(need + 1 > cap) return 0;
    if(n) out[0] = '\n';
    memcpy(out + n, rg_recover_note, sizeof(rg_recover_note));
    return need;
}
