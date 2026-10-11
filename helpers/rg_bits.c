#include "rg_bits.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_BITSTREAM || RG_FEATURE_MULTI_COMPARE

#include <string.h>

int rg_bits_bit(const RgFrame* frame, size_t i) {
    if(i >= frame->bit_count || i >= RG_ANALYZER_MAX_BITS) return -1;
    return (frame->bits[i / 8u] & (0x80u >> (i % 8u))) ? 1 : 0;
}

bool rg_bits_usable(const RgFrame* frame) {
    return frame->fit >= RG_ANALYZER_GOOD_FIT && frame->bit_count > 0;
}

size_t rg_bits_reference(const RgAnalysis* r) {
    if(r->group_count > 0 && r->groups[0].frame < r->frames_kept) return r->groups[0].frame;
    for(size_t i = 0; i < r->frames_kept; i++)
        if(rg_bits_usable(&r->frames[i])) return i;
    return r->frames_kept;
}

int rg_bits_similarity(const RgFrame* a, const RgFrame* b, int* shift) {
    if(shift) *shift = 0;
    if(a->bit_count == 0 || b->bit_count == 0) return 0;
    char sa[RG_ANALYZER_MAX_BITS + 1];
    char sb[RG_ANALYZER_MAX_BITS + 1];
    rg_analyzer_frame_bits(a, sa);
    rg_analyzer_frame_bits(b, sb);
    size_t na = strlen(sa), nb = strlen(sb);
    int s = 0;
    size_t overlap = 0;
    size_t m = rg_analyzer_align(sa, na, sb, nb, RG_ANALYZER_MAX_SHIFT, &s, &overlap);
    if(shift) *shift = s;
    size_t longer = na > nb ? na : nb;
    return (int)((m * 100u) / longer);
}

size_t rg_bits_repeats(const RgAnalysis* r, size_t i) {
    if(i >= r->frames_kept) return 0;
    uint8_t g = r->frames[i].group;
    if(g == RG_ANALYZER_NO_GROUP || g >= r->group_count) return 1;
    size_t n = (size_t)r->groups[g].exact + r->groups[g].aligned;
    return n ? n : 1;
}

char rg_bits_pattern(const RgAnalysis* r, size_t i) {
    if(i >= r->frames_kept) return '-';
    uint8_t g = r->frames[i].group;
    if(g == RG_ANALYZER_NO_GROUP || g >= r->group_count) return '-';
    return (char)('A' + g);
}

static bool rg_bits_comparable_pair(const RgFrame* sel, const RgFrame* f) {
    return rg_bits_usable(f) && !f->truncated && f->bit_count == sel->bit_count;
}

size_t rg_bits_comparable(const RgAnalysis* r, size_t sel) {
    if(sel >= r->frames_kept) return 0;
    const RgFrame* s = &r->frames[sel];
    size_t n = 0;
    for(size_t i = 0; i < r->frames_kept; i++)
        if(rg_bits_comparable_pair(s, &r->frames[i])) n++;
    return n;
}

size_t rg_bits_diff(
    const RgAnalysis* r,
    size_t sel,
    char* markers,
    size_t* constant,
    size_t* changing,
    size_t* unknown) {
    size_t c_const = 0, c_chg = 0, c_unk = 0;
    size_t n = 0;
    markers[0] = '\0';
    if(sel < r->frames_kept) {
        const RgFrame* s = &r->frames[sel];
        size_t len = s->bit_count > RG_ANALYZER_MAX_BITS ? RG_ANALYZER_MAX_BITS : s->bit_count;
        n = rg_bits_comparable(r, sel);
        // XOR of every comparable frame against the first one: a set bit
        // means some frame differs there.
        uint8_t diff[RG_ANALYZER_MAX_BITS / 8];
        memset(diff, 0, sizeof(diff));
        const RgFrame* first = NULL;
        for(size_t i = 0; i < r->frames_kept && n >= 2; i++) {
            const RgFrame* f = &r->frames[i];
            if(!rg_bits_comparable_pair(s, f)) continue;
            if(!first) {
                first = f;
                continue;
            }
            for(size_t b = 0; b < sizeof(diff); b++)
                diff[b] |= (uint8_t)(first->bits[b] ^ f->bits[b]);
        }
        for(size_t k = 0; k < len; k++) {
            char m;
            if(n < 2) {
                m = RG_BITS_UNKNOWN;
                c_unk++;
            } else if(diff[k / 8u] & (0x80u >> (k % 8u))) {
                m = RG_BITS_CHANGES;
                c_chg++;
            } else {
                m = RG_BITS_CONST;
                c_const++;
            }
            markers[k] = m;
        }
        markers[len] = '\0';
    }
    if(constant) *constant = c_const;
    if(changing) *changing = c_chg;
    if(unknown) *unknown = c_unk;
    return n;
}

void rg_bits_bytes(const RgFrame* frame, size_t align, RgBitsBytes* out) {
    size_t n = frame->bit_count > RG_ANALYZER_MAX_BITS ? RG_ANALYZER_MAX_BITS : frame->bit_count;
    align %= 8u;
    if(align > n) align = n;
    out->lead = align;
    out->bytes = (n - align) / 8u;
    out->tail = (n - align) % 8u;
}

bool rg_bits_byte(const RgFrame* frame, size_t align, size_t index, uint8_t* value) {
    RgBitsBytes layout;
    rg_bits_bytes(frame, align, &layout);
    if(index >= layout.bytes) return false;
    size_t start = layout.lead + index * 8u;
    uint8_t v = 0;
    for(size_t k = 0; k < 8u; k++)
        v = (uint8_t)((v << 1) | (uint8_t)rg_bits_bit(frame, start + k));
    *value = v;
    return true;
}

RgBitsFieldStatus rg_bits_field(const RgFrame* frame, size_t start, size_t end, RgBitsField* out) {
    memset(out, 0, sizeof(*out));
    if(frame->bit_count == 0) return RgBitsFieldEmpty;
    if(start > end || end >= frame->bit_count || end >= RG_ANALYZER_MAX_BITS)
        return RgBitsFieldRange;
    out->start = start;
    out->end = end;
    out->length = end - start + 1u;
    out->has_value = out->length <= RG_BITS_FIELD_MAX_VALUE_BITS;
    if(out->has_value) {
        uint64_t v = 0;
        for(size_t k = start; k <= end; k++)
            v = (v << 1) | (uint64_t)rg_bits_bit(frame, k);
        out->value = v;
    }
    return RgBitsFieldOk;
}

bool rg_bits_field_binary(const RgFrame* frame, const RgBitsField* field, char* out, size_t size) {
    if(size == 0) return false;
    out[0] = '\0';
    if(field->length == 0) return true;
    bool fits = field->length < size;
    size_t shown = fits ? field->length : (size > 3 ? size - 3 : 0);
    for(size_t k = 0; k < shown; k++)
        out[k] = rg_bits_bit(frame, field->start + k) ? '1' : '0';
    if(fits) {
        out[shown] = '\0';
    } else if(size >= 3) {
        out[shown] = '.';
        out[shown + 1] = '.';
        out[shown + 2] = '\0';
    } else {
        out[0] = '\0';
    }
    return fits;
}

void rg_bits_field_hex(const RgBitsField* field, char* out, size_t size) {
    if(size == 0) return;
    out[0] = '\0';
    if(!field->has_value || field->length == 0) return;
    size_t digits = (field->length + 3u) / 4u;
    if(digits + 1 > size) digits = size - 1;
    for(size_t d = 0; d < digits; d++) {
        size_t shift = (digits - 1u - d) * 4u;
        out[d] = "0123456789ABCDEF"[(field->value >> shift) & 0xFu];
    }
    out[digits] = '\0';
}

#endif
