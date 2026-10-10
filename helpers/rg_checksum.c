#include "rg_checksum.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_CHECKSUM_HINTS

#include <stdio.h>
#include <string.h>

/* Common CRC-8 polynomials (normal form): ATM/SMBus, Dallas/Maxim (reversed
 * 0x8C), SAE J1850, AUTOSAR, WCDMA, DVB-S2. */
static const uint8_t rg_crc8_polys[] = {0x07, 0x31, 0x1D, 0x2F, 0x9B, 0xD5};
static const uint8_t rg_crc8_inits[] = {0x00, 0xFF};

#define RG_CHECKSUM_MAX_END_SKIP 2u

void rg_checksum_set_init(RgChecksumSet* set) {
    memset(set, 0, sizeof(*set));
}

static bool rg_bit(const uint8_t* frame, size_t i) {
    return (frame[i / 8] >> (7 - i % 8)) & 1u;
}

bool rg_checksum_set_add(RgChecksumSet* set, const char* bits, size_t n) {
    if(n == 0 || n > RG_CHECKSUM_MAX_BITS || set->count >= RG_CHECKSUM_MAX_FRAMES) return false;
    if(set->count > 0 && n != set->bit_count) return false;
    uint8_t packed[RG_CHECKSUM_MAX_BITS / 8];
    memset(packed, 0, sizeof(packed));
    for(size_t i = 0; i < n; i++) {
        if(bits[i] == '1') packed[i / 8] |= (uint8_t)(0x80u >> (i % 8));
    }
    for(size_t f = 0; f < set->count; f++) {
        if(memcmp(set->frame[f], packed, sizeof(packed)) == 0) return false;
    }
    memcpy(set->frame[set->count], packed, sizeof(packed));
    set->bit_count = n;
    set->count++;
    return true;
}

uint8_t rg_checksum_crc8(const uint8_t* data, size_t len, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ poly) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t rg_word(const uint8_t* frame, size_t start, size_t width) {
    uint32_t v = 0;
    for(size_t i = 0; i < width; i++)
        v = (v << 1) | (rg_bit(frame, start + i) ? 1u : 0u);
    return v;
}

/* Distinct values of bits [from, to) across the set. */
static size_t rg_distinct(const RgChecksumSet* set, size_t from, size_t to) {
    size_t distinct = 0;
    for(size_t f = 0; f < set->count; f++) {
        bool seen = false;
        for(size_t g = 0; g < f && !seen; g++) {
            bool same = true;
            for(size_t i = from; i < to && same; i++) {
                same = rg_bit(set->frame[f], i) == rg_bit(set->frame[g], i);
            }
            seen = same;
        }
        if(!seen) distinct++;
    }
    return distinct;
}

/* Value the checksum would have for one frame. */
static uint32_t rg_expected(
    const uint8_t* frame,
    RgChecksumKind kind,
    size_t width,
    size_t data_start,
    size_t words,
    uint8_t poly,
    uint8_t init) {
    uint32_t mask = (1u << width) - 1u;
    if(kind == RgChecksumCrc8) {
        uint8_t bytes[RG_CHECKSUM_MAX_BITS / 8];
        for(size_t w = 0; w < words; w++)
            bytes[w] = (uint8_t)rg_word(frame, data_start + w * 8, 8);
        return rg_checksum_crc8(bytes, words, poly, init);
    }
    uint32_t x = 0, sum = 0;
    for(size_t w = 0; w < words; w++) {
        uint32_t v = rg_word(frame, data_start + w * width, width);
        x ^= v;
        sum += v;
    }
    switch(kind) {
    case RgChecksumXor:
        return x & mask;
    case RgChecksumXorInv:
        return ~x & mask;
    case RgChecksumSum:
        return sum & mask;
    case RgChecksumSumInv:
        return ~sum & mask;
    case RgChecksumSumNeg:
        return (0u - sum) & mask;
    default:
        return 0;
    }
}

static void rg_add_hit(RgChecksumResult* out, const RgChecksumHit* hit) {
    if(out->hit_count < RG_CHECKSUM_MAX_HITS) out->hit[out->hit_count++] = *hit;
}

static void
    rg_test_words(const RgChecksumSet* set, size_t width, size_t end_skip, RgChecksumResult* out) {
    size_t n = set->bit_count;
    if(n < end_skip + width) return;
    size_t ck_start = n - end_skip - width;
    size_t words = ck_start / width;
    if(words < 2) return;
    size_t data_start = ck_start - words * width;
    size_t need = width == 8 ? RG_CHECKSUM_MIN_BYTE : RG_CHECKSUM_MIN_NIBBLE;
    // The covered bits must differ between the frames, or every frame is the
    // same test and proves nothing.
    if(rg_distinct(set, data_start, ck_start + width) < need) return;

    for(int kind = RgChecksumXor; kind <= RgChecksumCrc8; kind++) {
        if(kind == RgChecksumCrc8 && width != 8) continue;
        size_t variants = kind == RgChecksumCrc8 ? sizeof(rg_crc8_polys) * sizeof(rg_crc8_inits) :
                                                   1;
        for(size_t v = 0; v < variants; v++) {
            uint8_t poly = rg_crc8_polys[v / sizeof(rg_crc8_inits)];
            uint8_t init = rg_crc8_inits[v % sizeof(rg_crc8_inits)];
            bool all = true;
            for(size_t f = 0; f < set->count && all; f++) {
                uint32_t want = rg_word(set->frame[f], ck_start, width);
                all = rg_expected(
                          set->frame[f],
                          (RgChecksumKind)kind,
                          width,
                          data_start,
                          words,
                          poly,
                          init) == want;
            }
            if(!all) continue;
            RgChecksumHit hit = {
                .kind = (RgChecksumKind)kind,
                .width = (uint8_t)width,
                .poly = kind == RgChecksumCrc8 ? poly : 0,
                .init = kind == RgChecksumCrc8 ? init : 0,
                .end_skip = (uint8_t)end_skip,
                .data_bits = (uint16_t)(words * width),
                .frames = (uint16_t)set->count,
            };
            rg_add_hit(out, &hit);
        }
    }
}

static void rg_test_parity(const RgChecksumSet* set, size_t end_skip, RgChecksumResult* out) {
    size_t n = set->bit_count;
    if(n < end_skip + 9) return;
    size_t pbit = n - end_skip - 1;
    if(rg_distinct(set, 0, pbit + 1) < RG_CHECKSUM_MIN_PARITY) return;
    for(int odd = 0; odd < 2; odd++) {
        bool all = true;
        for(size_t f = 0; f < set->count && all; f++) {
            unsigned parity = 0;
            for(size_t i = 0; i < pbit; i++)
                parity ^= rg_bit(set->frame[f], i);
            all = ((parity ^ (unsigned)odd) & 1u) == (rg_bit(set->frame[f], pbit) ? 1u : 0u);
        }
        if(!all) continue;
        RgChecksumHit hit = {
            .kind = odd ? RgChecksumParityOdd : RgChecksumParityEven,
            .width = 1,
            .end_skip = (uint8_t)end_skip,
            .data_bits = (uint16_t)pbit,
            .frames = (uint16_t)set->count,
        };
        rg_add_hit(out, &hit);
    }
}

void rg_checksum_analyze(const RgChecksumSet* set, RgChecksumResult* out) {
    memset(out, 0, sizeof(*out));
    out->frames = set->count;
    out->bit_count = set->bit_count;
    out->enough_for_byte = set->count >= RG_CHECKSUM_MIN_BYTE;
    if(!out->enough_for_byte) return;
    for(size_t e = 0; e <= RG_CHECKSUM_MAX_END_SKIP; e++) {
        rg_test_words(set, 8, e, out);
        rg_test_words(set, 4, e, out);
        rg_test_parity(set, e, out);
    }
}

void rg_checksum_describe(const RgChecksumHit* hit, char* out, size_t out_size) {
    if(!out || out_size == 0) return;
    char tail[24] = "";
    if(hit->end_skip) {
        snprintf(
            tail, sizeof(tail), " (+%u bit%s after)", hit->end_skip, hit->end_skip > 1 ? "s" : "");
    }
    const char* unit = hit->width == 8 ? "bytes" : "nibbles";
    unsigned words = hit->width ? hit->data_bits / hit->width : 0;
    switch(hit->kind) {
    case RgChecksumXor:
    case RgChecksumXorInv:
    case RgChecksumSum:
    case RgChecksumSumInv:
    case RgChecksumSumNeg: {
        static const char* const names[] = {
            "XOR", "inverted XOR", "sum", "inverted sum", "negated sum"};
        snprintf(
            out,
            out_size,
            "last %u bits = %s of the %u %s before%s",
            hit->width,
            names[hit->kind],
            words,
            unit,
            tail);
        break;
    }
    case RgChecksumCrc8:
        snprintf(
            out,
            out_size,
            "last 8 bits = CRC-8 poly 0x%02X init 0x%02X of the %u bytes before%s",
            hit->poly,
            hit->init,
            words,
            tail);
        break;
    case RgChecksumParityEven:
    case RgChecksumParityOdd:
        snprintf(
            out,
            out_size,
            "last bit = %s parity of the %u bits before%s",
            hit->kind == RgChecksumParityEven ? "even" : "odd",
            hit->data_bits,
            tail);
        break;
    }
}

#endif /* RG_FEATURE_CHECKSUM_HINTS */
