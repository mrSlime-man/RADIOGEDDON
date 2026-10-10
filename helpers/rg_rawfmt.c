#include "rg_rawfmt.h"

#include <stdio.h>
#include <string.h>

static const char rg_rawfmt_key[] = "RAW_Data:";

void rg_rawfmt_init(RgRawFmt* f, uint32_t per_line) {
    f->per_line = per_line ? per_line : RG_RAWFMT_LINE_VALUES;
    f->in_line = 0;
}

size_t rg_rawfmt_header(char* out, size_t cap, uint32_t frequency, const char* preset) {
    int n = snprintf(
        out,
        cap,
        "Filetype: Flipper SubGhz RAW File\n"
        "Version: 1\n"
        "Frequency: %lu\n"
        "Preset: %s\n"
        "Protocol: RAW\n",
        (unsigned long)frequency,
        preset ? preset : "");
    if(n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

/* Decimal text of a signed value, without snprintf. Returns its length. */
static size_t rg_rawfmt_int(char* out, int32_t v) {
    char tmp[11];
    size_t n = 0;
    uint32_t mag = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    do {
        tmp[n++] = (char)('0' + mag % 10u);
        mag /= 10u;
    } while(mag);
    size_t len = 0;
    if(v < 0) out[len++] = '-';
    while(n)
        out[len++] = tmp[--n];
    return len;
}

size_t rg_rawfmt_values(
    RgRawFmt* f,
    const int32_t* values,
    size_t count,
    char* out,
    size_t cap,
    size_t* len) {
    size_t used = 0;
    while(used < count && cap - *len >= RG_RAWFMT_VALUE_MAX) {
        int32_t v = values[used++];
        if(v == 0) continue;
        if(f->in_line == 0) {
            memcpy(out + *len, rg_rawfmt_key, sizeof(rg_rawfmt_key) - 1);
            *len += sizeof(rg_rawfmt_key) - 1;
        }
        out[(*len)++] = ' ';
        *len += rg_rawfmt_int(out + *len, v);
        if(++f->in_line == f->per_line) {
            out[(*len)++] = '\n';
            f->in_line = 0;
        }
    }
    return used;
}

bool rg_rawfmt_end_line(RgRawFmt* f, char* out, size_t cap, size_t* len) {
    if(f->in_line == 0) return true;
    if(cap - *len < 1) return false;
    out[(*len)++] = '\n';
    f->in_line = 0;
    return true;
}

size_t rg_rawfmt_lost(char* out, size_t cap, uint32_t lost, uint32_t gaps, uint32_t first_gap) {
    if(lost == 0) return 0;
    int n = snprintf(
        out,
        cap,
        "# Lost: %lu samples in %lu gaps, first after sample %lu (SD card too slow); "
        "timing is not continuous across a gap\n",
        (unsigned long)lost,
        (unsigned long)gaps,
        (unsigned long)first_gap);
    if(n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}
