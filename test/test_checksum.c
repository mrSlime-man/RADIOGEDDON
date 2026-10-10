/**
 * Host-side unit tests for checksum / CRC structure hypotheses
 * (helpers/rg_checksum.c): known structures are found, chance and degenerate
 * cases are not.
 * Run: make -C test
 */
#include "../helpers/rg_checksum.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

static uint32_t g_rng = 99u;
static uint8_t rnd8(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (uint8_t)(g_rng >> 16);
}

/* Bytes to a '0'/'1' string, with @p pre leading bits and @p post trailing bits. */
static size_t
    to_bits(const uint8_t* bytes, size_t n, const char* pre, const char* post, char* out) {
    size_t k = 0;
    for(const char* p = pre; *p; p++)
        out[k++] = *p;
    for(size_t i = 0; i < n; i++)
        for(int b = 7; b >= 0; b--)
            out[k++] = ((bytes[i] >> b) & 1) ? '1' : '0';
    for(const char* p = post; *p; p++)
        out[k++] = *p;
    out[k] = '\0';
    return k;
}

static bool has(const RgChecksumResult* r, RgChecksumKind kind, uint8_t width, uint8_t skip) {
    for(size_t i = 0; i < r->hit_count; i++) {
        if(r->hit[i].kind == kind && r->hit[i].width == width && r->hit[i].end_skip == skip)
            return true;
    }
    return false;
}

static void test_crc8_vectors(void) {
    printf("test_crc8_vectors\n");
    const uint8_t check[] = "123456789";
    // Catalogue check values (without the final XOR where the catalogue has one).
    CHECK(rg_checksum_crc8(check, 9, 0x07, 0x00) == 0xF4, "CRC-8/ATM");
    CHECK(rg_checksum_crc8(check, 9, 0x1D, 0xFF) == (0x4B ^ 0xFF), "CRC-8/SAE-J1850");
    CHECK(rg_checksum_crc8(check, 9, 0x2F, 0xFF) == (0xDF ^ 0xFF), "CRC-8/AUTOSAR");
    CHECK(rg_checksum_crc8(check, 0, 0x07, 0x5A) == 0x5A, "empty input");
}

static void test_xor_byte(void) {
    printf("test_xor_byte\n");
    RgChecksumSet set;
    rg_checksum_set_init(&set);
    char bits[RG_CHECKSUM_MAX_BITS + 1];
    for(int f = 0; f < 5; f++) {
        uint8_t b[5] = {0xA5, rnd8(), rnd8(), rnd8(), 0};
        b[4] = b[0] ^ b[1] ^ b[2] ^ b[3];
        // Two sync bits in front: words count back from the checksum.
        size_t n = to_bits(b, 5, "10", "", bits);
        CHECK(rg_checksum_set_add(&set, bits, n), "frame added");
    }
    RgChecksumResult r;
    rg_checksum_analyze(&set, &r);
    CHECK(r.frames == 5 && r.bit_count == 42, "five 42-bit frames");
    CHECK(has(&r, RgChecksumXor, 8, 0), "XOR byte found");
    char text[96];
    for(size_t i = 0; i < r.hit_count; i++) {
        if(r.hit[i].kind == RgChecksumXor && r.hit[i].width == 8) {
            rg_checksum_describe(&r.hit[i], text, sizeof(text));
            CHECK(strcmp(text, "last 8 bits = XOR of the 4 bytes before") == 0, "description");
        }
    }
}

static void test_crc_and_sum(void) {
    printf("test_crc_and_sum\n");
    RgChecksumSet set;
    rg_checksum_set_init(&set);
    char bits[RG_CHECKSUM_MAX_BITS + 1];
    for(int f = 0; f < 4; f++) {
        uint8_t b[4] = {rnd8(), rnd8(), rnd8(), 0};
        b[3] = rg_checksum_crc8(b, 3, 0x31, 0xFF);
        // A stop bit after the checksum.
        size_t n = to_bits(b, 4, "", "1", bits);
        rg_checksum_set_add(&set, bits, n);
    }
    RgChecksumResult r;
    rg_checksum_analyze(&set, &r);
    bool crc = false;
    for(size_t i = 0; i < r.hit_count; i++) {
        crc |= r.hit[i].kind == RgChecksumCrc8 && r.hit[i].poly == 0x31 && r.hit[i].init == 0xFF &&
               r.hit[i].end_skip == 1;
    }
    CHECK(crc, "CRC-8 0x31/0xFF found one bit before the end");

    rg_checksum_set_init(&set);
    for(int f = 0; f < 6; f++) {
        uint8_t b[4] = {rnd8(), rnd8(), rnd8(), 0};
        b[3] = (uint8_t)(0u - (unsigned)(b[0] + b[1] + b[2]));
        size_t n = to_bits(b, 4, "", "", bits);
        rg_checksum_set_add(&set, bits, n);
    }
    rg_checksum_analyze(&set, &r);
    CHECK(has(&r, RgChecksumSumNeg, 8, 0), "negated sum found");
    CHECK(!has(&r, RgChecksumXor, 8, 0), "not XOR");
}

static void test_nibble_and_parity(void) {
    printf("test_nibble_and_parity\n");
    RgChecksumSet set;
    rg_checksum_set_init(&set);
    char bits[RG_CHECKSUM_MAX_BITS + 1];
    // 20 data bits in nibbles, then a 4-bit sum.
    for(unsigned f = 0; f < RG_CHECKSUM_MIN_NIBBLE; f++) {
        unsigned nib[5], sum = 0;
        size_t k = 0;
        for(int i = 0; i < 5; i++) {
            nib[i] = rnd8() & 0xF;
            sum += nib[i];
            for(int b = 3; b >= 0; b--)
                bits[k++] = ((nib[i] >> b) & 1) ? '1' : '0';
        }
        for(int b = 3; b >= 0; b--)
            bits[k++] = (((sum & 0xF) >> b) & 1) ? '1' : '0';
        bits[k] = '\0';
        rg_checksum_set_add(&set, bits, k);
    }
    RgChecksumResult r;
    rg_checksum_analyze(&set, &r);
    CHECK(has(&r, RgChecksumSum, 4, 0), "4-bit sum found");

    // Odd parity over 15 bits needs RG_CHECKSUM_MIN_PARITY frames.
    rg_checksum_set_init(&set);
    for(unsigned f = 0; f < RG_CHECKSUM_MIN_PARITY; f++) {
        unsigned ones = 0;
        size_t k = 0;
        for(int i = 0; i < 15; i++) {
            int bit = rnd8() & 1;
            ones += (unsigned)bit;
            bits[k++] = bit ? '1' : '0';
        }
        bits[k++] = (ones % 2 == 0) ? '1' : '0'; // odd parity
        bits[k] = '\0';
        rg_checksum_set_add(&set, bits, k);
    }
    rg_checksum_analyze(&set, &r);
    CHECK(r.frames == RG_CHECKSUM_MIN_PARITY, "parity frames collected");
    CHECK(has(&r, RgChecksumParityOdd, 1, 0), "odd parity found");
    CHECK(!has(&r, RgChecksumParityEven, 1, 0), "not even parity");
}

static void test_no_false_hits(void) {
    printf("test_no_false_hits\n");
    RgChecksumSet set;
    char bits[RG_CHECKSUM_MAX_BITS + 1];
    RgChecksumResult r;
    // Random frames: over many trials, no structure claimed.
    int claimed = 0;
    for(int trial = 0; trial < 200; trial++) {
        rg_checksum_set_init(&set);
        for(int f = 0; f < 4; f++) {
            uint8_t b[5] = {rnd8(), rnd8(), rnd8(), rnd8(), rnd8()};
            size_t n = to_bits(b, 5, "", "", bits);
            rg_checksum_set_add(&set, bits, n);
        }
        rg_checksum_analyze(&set, &r);
        if(r.hit_count) claimed++;
    }
    CHECK(claimed == 0, "random frames: no checksum claimed in 200 trials");

    // Too few distinct frames: repeats count once.
    rg_checksum_set_init(&set);
    uint8_t b[4] = {1, 2, 3, 0};
    b[3] = b[0] ^ b[1] ^ b[2];
    size_t n = to_bits(b, 4, "", "", bits);
    CHECK(rg_checksum_set_add(&set, bits, n), "first frame");
    CHECK(!rg_checksum_set_add(&set, bits, n), "repeat ignored");
    rg_checksum_analyze(&set, &r);
    CHECK(!r.enough_for_byte && r.hit_count == 0, "one frame: nothing claimed");

    // Frames that differ only outside the covered bits prove nothing.
    rg_checksum_set_init(&set);
    for(int f = 0; f < 6; f++) {
        char pre[3] = {(f & 1) ? '1' : '0', (f & 2) ? '1' : '0', '\0'};
        char post[3] = {(f & 4) ? '1' : '0', '1', '\0'};
        n = to_bits(b, 4, pre, post, bits);
        rg_checksum_set_add(&set, bits, n);
    }
    rg_checksum_analyze(&set, &r);
    CHECK(r.frames == 6, "six distinct frames");
    bool skip0 = false;
    for(size_t i = 0; i < r.hit_count; i++)
        skip0 |= r.hit[i].end_skip == 2 && r.hit[i].width == 8;
    CHECK(!skip0, "covered bits identical: not claimed");

    // Another length is not added.
    rg_checksum_set_init(&set);
    rg_checksum_set_add(&set, "10101010", 8);
    CHECK(!rg_checksum_set_add(&set, "1010101011", 10), "different length refused");
    CHECK(!rg_checksum_set_add(&set, "", 0), "empty refused");
}

int main(void) {
    test_crc8_vectors();
    test_xor_byte();
    test_crc_and_sum();
    test_nibble_and_parity();
    test_no_false_hits();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
