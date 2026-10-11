/**
 * Host-side test of the signal-analysis engine (helpers/rg_analyzer.c) on
 * real captures: the Flipper Zero firmware's own RAW Sub-GHz test captures,
 * each of which the firmware's decoder test pairs with a protocol (the same
 * 50 pairs as test_fwdecode.c). Build & run via `make -C test captures`.
 *
 * The captures and the protocols' decoder sources come from the pinned
 * Official release, downloaded and checked by test/firmware/fetch.sh; they
 * are not committed. They are third-party test data: nothing here was
 * captured or checked on our hardware.
 *
 * Each capture is analysed as the app analyses a file: streamed through
 * RgRawReader, rewound for every pass. The engine never sees the protocol.
 * Its hypotheses are then scored against what each protocol's decoder source
 * says, read from the source at run time:
 *
 *  - Te: within the decoder's own tolerance (te_delta) of its te_short;
 *  - encoding family: Manchester exactly when the decoder uses the
 *    firmware's Manchester state machine (manchester_advance); PWM and PPM
 *    are both counted as the pulse-width family, since many remotes fit both;
 *  - frame length: within one bit of the decoder's min_count_bit_for_found.
 *
 * The scores are measurements, not pass/fail per capture: a few captures are
 * too noisy or too unusual for a protocol-blind engine, and some protocols
 * count bits differently from the frame on air. The test fails when a total
 * drops below the MIN_* figures below (what the engine scored when they were
 * set) or when an output breaks one of the engine's own rules.
 */
#include "../helpers/rg_analyzer.h"
#include "../helpers/rg_raw.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

#define FW_SUBGHZ "build/fw/applications/debug/unit_tests/resources/unit_tests/subghz"
#define FW_PROTO  "build/fw/lib/subghz/protocols"

/* Totals the engine reached on these 50 captures when they were set. Raise
 * them when the engine improves; a drop is a regression. */
#define MIN_TE_OK     50
#define MIN_FAMILY_OK 43
#define MIN_BITS_OK   35

typedef struct {
    const char* capture; /* as subghz_test.c names it */
    const char* source; /* the decoder that test expects, under FW_PROTO */
} Pair;

/* In the order subghz_test.c runs them (see test_fwdecode.c). */
static const Pair pairs[] = {
    {"came_atomo_raw.sub", "came_atomo.c"},
    {"came_raw.sub", "came.c"},
    {"came_twee_raw.sub", "came_twee.c"},
    {"faac_slh_raw.sub", "faac_slh.c"},
    {"gate_tx_raw.sub", "gate_tx.c"},
    {"hormann_hsm_raw.sub", "hormann.c"},
    {"ido_117_111_raw.sub", "ido.c"},
    {"doorhan_raw.sub", "keeloq.c"},
    {"kia_seed_raw.sub", "kia.c"},
    {"nero_radio_raw.sub", "nero_radio.c"},
    {"nero_sketch_raw.sub", "nero_sketch.c"},
    {"nice_flo_raw.sub", "nice_flo.c"},
    {"nice_flor_s_raw.sub", "nice_flor_s.c"},
    {"Princeton_raw.sub", "princeton.c"},
    {"scher_khan_magic_code.sub", "scher_khan.c"},
    {"Somfy_keytis_raw.sub", "somfy_keytis.c"},
    {"somfy_telis_raw.sub", "somfy_telis.c"},
    {"cenmax_raw.sub", "star_line.c"},
    {"linear_raw.sub", "linear.c"},
    {"linear_delta3_raw.sub", "linear_delta3.c"},
    {"megacode_raw.sub", "megacode.c"},
    {"security_pls_1_0_raw.sub", "secplus_v1.c"},
    {"security_pls_2_0_raw.sub", "secplus_v2.c"},
    {"holtek_raw.sub", "holtek.c"},
    {"power_smart_raw.sub", "power_smart.c"},
    {"marantec_raw.sub", "marantec.c"},
    {"bett_raw.sub", "bett.c"},
    {"doitrand_raw.sub", "doitrand.c"},
    {"phoenix_v2_raw.sub", "phoenix_v2.c"},
    {"honeywell_wdb_raw.sub", "honeywell_wdb.c"},
    {"magellan_raw.sub", "magellan.c"},
    {"intertechno_v3_raw.sub", "intertechno_v3.c"},
    {"clemsa_raw.sub", "clemsa.c"},
    {"ansonic_raw.sub", "ansonic.c"},
    {"smc5326_raw.sub", "smc5326.c"},
    {"holtek_ht12x_raw.sub", "holtek_ht12x.c"},
    {"dooya_raw.sub", "dooya.c"},
    {"alutech_at_4n_raw.sub", "alutech_at_4n.c"},
    {"nice_one_raw.sub", "nice_flor_s.c"},
    {"kinggates_stylo4k_raw.sub", "kinggates_stylo_4k.c"},
    {"mastercode_raw.sub", "mastercode.c"},
    {"dickert_raw.sub", "dickert_mahs.c"},
    {"roger_raw.sub", "roger.c"},
    {"hollarm_raw.sub", "hollarm.c"},
    {"revers_rb2_raw.sub", "revers_rb2.c"},
    {"gangqi_raw.sub", "gangqi.c"},
    {"hay21_raw.sub", "hay21.c"},
    {"feron_raw.sub", "feron.c"},
    {"legrand_raw.sub", "legrand.c"},
    {"marantec24_raw.sub", "marantec24.c"},
};
#define PAIR_COUNT (sizeof(pairs) / sizeof(pairs[0]))

/* ---- What the decoder source says ----------------------------------------- */

typedef struct {
    unsigned te_short;
    unsigned te_delta;
    unsigned min_bits;
    bool manchester;
} Truth;

static char* read_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if(s && fread(s, 1, (size_t)n, f) != (size_t)n) {
        free(s);
        s = NULL;
    }
    if(s) s[n] = '\0';
    fclose(f);
    return s;
}

/* The value of ".name = N" in the decoder's SubGhzBlockConst initialiser
 * (the code reads the same fields elsewhere as const.te_short and so on). */
static bool field(const char* block, const char* name, unsigned* out) {
    char key[48];
    snprintf(key, sizeof(key), ".%s", name);
    const char* p = strstr(block, key);
    if(!p || strstr(p + 1, key)) return false;
    p += strlen(key);
    while(*p == ' ' || *p == '=')
        p++;
    char* end;
    unsigned long v = strtoul(p, &end, 10);
    if(end == p) return false;
    *out = (unsigned)v;
    return true;
}

static bool read_truth(const char* source, Truth* t) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FW_PROTO, source);
    char* src = read_all(path);
    if(!src) return false;
    bool ok = false;
    /* Exactly one SubGhzBlockConst initialiser: "... SubGhzBlockConst x = { ... };" */
    const char* decl = strstr(src, "SubGhzBlockConst");
    const char* open = decl ? strchr(decl, '{') : NULL;
    const char* close = open ? strchr(open, '}') : NULL;
    if(close && !strstr(close, "SubGhzBlockConst")) {
        size_t n = (size_t)(close - open);
        char* block = malloc(n + 1);
        memcpy(block, open, n);
        block[n] = '\0';
        ok = field(block, "te_short", &t->te_short) && field(block, "te_delta", &t->te_delta) &&
             field(block, "min_count_bit_for_found", &t->min_bits);
        free(block);
    }
    t->manchester = strstr(src, "manchester_advance") != NULL;
    free(src);
    return ok;
}

/* ---- Running the engine as the app does ----------------------------------- */

static size_t file_read(void* ctx, uint8_t* buf, size_t len) {
    return fread(buf, 1, len, (FILE*)ctx);
}

static bool file_seek(void* ctx, uint32_t offset) {
    return fseek((FILE*)ctx, (long)offset, SEEK_SET) == 0;
}

/* The firmware's test names two captures with a capital letter; the card is
 * case-insensitive, the host is not. */
static bool find_capture(const char* name, char* path, size_t cap) {
    DIR* d = opendir(FW_SUBGHZ);
    if(!d) return false;
    struct dirent* e;
    bool found = false;
    while((e = readdir(d)) != NULL) {
        if(strcasecmp(e->d_name, name) == 0) {
            snprintf(path, cap, "%s/%s", FW_SUBGHZ, e->d_name);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

/* Streams the capture through every pass; also returns all its samples so
 * the in-memory run can be compared. */
static bool analyse(const char* path, RgAnalyzer* a, int32_t** samples, size_t* count) {
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    RgRawReader* r = malloc(sizeof(RgRawReader));
    RgRawSource src = {file_read, file_seek, f};
    rg_raw_reader_init(r, src);
    size_t cap = 4096, n = 0;
    int32_t* all = malloc(cap * sizeof(int32_t));
    int32_t chunk[128];
    rg_analyzer_begin(a);
    bool first = true, again;
    do {
        size_t k;
        while((k = rg_raw_reader_read(r, chunk, 128)) > 0) {
            rg_analyzer_feed(a, chunk, k);
            if(!first) continue;
            if(n + k > cap) {
                cap *= 2;
                all = realloc(all, cap * sizeof(int32_t));
            }
            memcpy(all + n, chunk, k * sizeof(int32_t));
            n += k;
        }
        first = false;
        again = rg_analyzer_next_pass(a);
        if(again) rg_raw_reader_rewind(r);
    } while(again);
    bool ok = r->any_data && !r->corrupt;
    free(r);
    fclose(f);
    *samples = all;
    *count = n;
    return ok;
}

static unsigned diff(unsigned a, unsigned b) {
    return a > b ? a - b : b - a;
}

int main(void) {
    printf("test_fwanalyze: rg_analyzer on the firmware's %zu paired RAW captures\n", PAIR_COUNT);
    RgAnalyzer* a = malloc(sizeof(RgAnalyzer));
    RgAnalysis* mem = malloc(sizeof(RgAnalysis));
    unsigned te_ok = 0, family_ok = 0, bits_ok = 0, unknown = 0, analysed = 0;

    printf(
        "\n%-26s %-12s %-16s %-16s %s\n",
        "capture",
        "Te us",
        "encoding",
        "bits",
        "(engine / decoder source)");
    for(size_t i = 0; i < PAIR_COUNT; i++) {
        const Pair* p = &pairs[i];
        char path[512], msg[160];
        Truth t;
        bool have_truth = read_truth(p->source, &t);
        snprintf(
            msg, sizeof(msg), "%s: Te, tolerance and bits read from %s", p->capture, p->source);
        CHECK(have_truth, msg);
        bool have_file = find_capture(p->capture, path, sizeof(path));
        snprintf(msg, sizeof(msg), "%s: capture present", p->capture);
        CHECK(have_file, msg);
        if(!have_truth || !have_file) continue;

        int32_t* samples = NULL;
        size_t count = 0;
        bool read_ok = analyse(path, a, &samples, &count);
        snprintf(msg, sizeof(msg), "%s: RAW data read in full", p->capture);
        CHECK(read_ok && count > 0, msg);
        const RgAnalysis* r = &a->result;
        analysed++;

        /* The engine's own rules. */
        snprintf(msg, sizeof(msg), "%s: Te is 0 or at least the glitch floor", p->capture);
        CHECK(r->te_us == 0 || r->te_us >= RG_ANALYZER_MIN_TE_US, msg);
        snprintf(msg, sizeof(msg), "%s: confidence never claims certainty", p->capture);
        CHECK(r->encoding_confidence >= 0 && r->encoding_confidence <= 95, msg);
        snprintf(msg, sizeof(msg), "%s: no encoding, no bits", p->capture);
        CHECK(
            r->encoding != RgEncodingUnknown || (r->bit_count == 0 && r->signal_frames == 0), msg);
        rg_analyzer_run(samples, count, mem);
        snprintf(msg, sizeof(msg), "%s: streamed passes match the in-memory run", p->capture);
        CHECK(
            mem->te_us == r->te_us && mem->encoding == r->encoding &&
                mem->bit_count == r->bit_count && mem->frame_count == r->frame_count &&
                mem->encoding_confidence == r->encoding_confidence,
            msg);
        free(samples);

        /* Scored against the decoder source. */
        bool te = r->te_us && diff(r->te_us, t.te_short) <= t.te_delta;
        bool family = r->encoding != RgEncodingUnknown &&
                      (r->encoding == RgEncodingManchester) == t.manchester;
        bool bits = r->bit_count && diff((unsigned)r->bit_count, t.min_bits) <= 1;
        te_ok += te;
        family_ok += family;
        bits_ok += bits;
        unknown += r->encoding == RgEncodingUnknown;
        printf(
            "%-26s %4lu/%-4u %-2s %-10s/%-2s %-2s %3zu/%-3u %-2s\n",
            p->capture,
            (unsigned long)r->te_us,
            t.te_short,
            te ? "ok" : "--",
            rg_analyzer_encoding_name(r->encoding),
            t.manchester ? "M" : "P",
            family ? "ok" : "--",
            r->bit_count,
            t.min_bits,
            bits ? "ok" : "--");
    }

    printf(
        "\n%u captures: Te within the decoder's tolerance for %u, encoding family right "
        "for %u (no guess for %u), frame length within one bit of the decoder's for %u\n",
        analysed,
        te_ok,
        family_ok,
        unknown,
        bits_ok);
    CHECK(analysed == PAIR_COUNT, "every paired capture analysed");
    CHECK(te_ok >= MIN_TE_OK, "Te score did not drop");
    CHECK(family_ok >= MIN_FAMILY_OK, "encoding family score did not drop");
    CHECK(bits_ok >= MIN_BITS_OK, "frame length score did not drop");

    free(mem);
    free(a);
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("CAPTURE ANALYSIS TESTS FAILED\n");
        return 1;
    }
    printf("ALL CAPTURE ANALYSIS TESTS PASSED\n");
    return 0;
}
