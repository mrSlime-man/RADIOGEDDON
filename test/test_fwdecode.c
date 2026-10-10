/**
 * Host-side test of RadioGeddon's RAW feeder (helpers/rg_decode.c) with the
 * firmware's own Sub-GHz decoders on the firmware's own RAW test captures.
 * Build & run via `make -C test decoders`.
 *
 *  - The Flipper Zero firmware's Sub-GHz receiver, environment, registry and
 *    every protocol it registers (lib/subghz) are compiled in unchanged, from
 *    the commit of the pinned Official release, over the stub Furi layer. The
 *    keystore is a stand-in that holds no keys (stubs/subghz_stub.c).
 *  - The captures are the firmware's Sub-GHz unit-test files, from the same
 *    commit, and the protocol expected from each is the one the firmware's
 *    decoder unit test (applications/debug/unit_tests/tests/subghz/
 *    subghz_test.c, checked against this list) names for it.
 *
 * Both are downloaded and checked by test/firmware/fetch.sh; they are not
 * committed. The captures are third-party test data: nothing here was
 * captured or checked on our hardware.
 *
 * Each capture is decoded as the app decodes: RgRawReader -> rg_decode_run ->
 * one receiver with every protocol, filtered to SubGhzProtocolFlag_Decodable,
 * in an environment with no keystore loaded and the rainbow table names NULL
 * (as radiogeddon_subghz_environment_acquire sets them; the firmware's test
 * loads both). Every decode is described with the app's
 * radiogeddon_decode_text, hashed and logged, then the receiver is reset, as
 * the app and the firmware's test do. For the two decoders whose own text
 * would read the NULL table name, that it still does is checked too (in a
 * child process, like every capture, so a crash is reported with the
 * protocol it was describing).
 *
 * Informational, not checked: whether the key of the folder's key file for
 * the same protocol is among those decoded, and how many more (or fewer)
 * decodes a quiet line after the capture would give (a second receiver, fed
 * the same samples with the last one held into a TAIL_GAP_US gap).
 */
#include "furi.h"
#include "../helpers/rg_decode.h"
#include "../helpers/rg_raw.h"
#include "../helpers/radiogeddon_decode_text.h"

#include <lib/subghz/receiver.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <lib/flipper_format/flipper_format.h>

#include <dirent.h>
#include <signal.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

#define FW_TESTS  "build/fw/applications/debug/unit_tests"
#define FW_SUBGHZ FW_TESTS "/resources/unit_tests/subghz"
#define FW_TEST_C FW_TESTS "/tests/subghz/subghz_test.c"
#define CHILD_LOG "build/fwdecode_child.log"

/* Seconds a capture may take before it counts as hung. */
#define RUN_TIMEOUT_S 120
/* The quiet line after a capture, for the informational second receiver. */
#define TAIL_GAP_US   100000u

/* ---- The firmware's decoder test list ------------------------------------ */

typedef enum {
    ExpectDecode, /* decodes, and the decoder's own text describes it */
    /* Decodes, but the decoder's own text reads the rainbow table name, which
     * the app leaves NULL: strcmp(NULL, "") in came_atomo.c and
     * alutech_at_4n.c, a NULL pointer dereference on the device too. The app
     * describes these from the decoded data (radiogeddon_decode_text.h). */
    ExpectDataOnly,
} Expect;

typedef struct {
    const char* file; /* as subghz_test.c names it */
    const char* constant; /* the protocol name's constant there */
    const char* protocol;
    Expect expect;
} Pair;

#define PAIR(file, name, expect) {file, #name, name, expect}

/* In the order subghz_test.c runs them. */
static const Pair pairs[] = {
    PAIR("came_atomo_raw.sub", SUBGHZ_PROTOCOL_CAME_ATOMO_NAME, ExpectDataOnly),
    PAIR("came_raw.sub", SUBGHZ_PROTOCOL_CAME_NAME, ExpectDecode),
    PAIR("came_twee_raw.sub", SUBGHZ_PROTOCOL_CAME_TWEE_NAME, ExpectDecode),
    PAIR("faac_slh_raw.sub", SUBGHZ_PROTOCOL_FAAC_SLH_NAME, ExpectDecode),
    PAIR("gate_tx_raw.sub", SUBGHZ_PROTOCOL_GATE_TX_NAME, ExpectDecode),
    PAIR("hormann_hsm_raw.sub", SUBGHZ_PROTOCOL_HORMANN_HSM_NAME, ExpectDecode),
    PAIR("ido_117_111_raw.sub", SUBGHZ_PROTOCOL_IDO_NAME, ExpectDecode),
    PAIR("doorhan_raw.sub", SUBGHZ_PROTOCOL_KEELOQ_NAME, ExpectDecode),
    PAIR("kia_seed_raw.sub", SUBGHZ_PROTOCOL_KIA_NAME, ExpectDecode),
    PAIR("nero_radio_raw.sub", SUBGHZ_PROTOCOL_NERO_RADIO_NAME, ExpectDecode),
    PAIR("nero_sketch_raw.sub", SUBGHZ_PROTOCOL_NERO_SKETCH_NAME, ExpectDecode),
    PAIR("nice_flo_raw.sub", SUBGHZ_PROTOCOL_NICE_FLO_NAME, ExpectDecode),
    PAIR("nice_flor_s_raw.sub", SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME, ExpectDecode),
    PAIR("Princeton_raw.sub", SUBGHZ_PROTOCOL_PRINCETON_NAME, ExpectDecode),
    PAIR("scher_khan_magic_code.sub", SUBGHZ_PROTOCOL_SCHER_KHAN_NAME, ExpectDecode),
    PAIR("Somfy_keytis_raw.sub", SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME, ExpectDecode),
    PAIR("somfy_telis_raw.sub", SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME, ExpectDecode),
    PAIR("cenmax_raw.sub", SUBGHZ_PROTOCOL_STAR_LINE_NAME, ExpectDecode),
    PAIR("linear_raw.sub", SUBGHZ_PROTOCOL_LINEAR_NAME, ExpectDecode),
    PAIR("linear_delta3_raw.sub", SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME, ExpectDecode),
    PAIR("megacode_raw.sub", SUBGHZ_PROTOCOL_MEGACODE_NAME, ExpectDecode),
    PAIR("security_pls_1_0_raw.sub", SUBGHZ_PROTOCOL_SECPLUS_V1_NAME, ExpectDecode),
    PAIR("security_pls_2_0_raw.sub", SUBGHZ_PROTOCOL_SECPLUS_V2_NAME, ExpectDecode),
    PAIR("holtek_raw.sub", SUBGHZ_PROTOCOL_HOLTEK_NAME, ExpectDecode),
    PAIR("power_smart_raw.sub", SUBGHZ_PROTOCOL_POWER_SMART_NAME, ExpectDecode),
    PAIR("marantec_raw.sub", SUBGHZ_PROTOCOL_MARANTEC_NAME, ExpectDecode),
    PAIR("bett_raw.sub", SUBGHZ_PROTOCOL_BETT_NAME, ExpectDecode),
    PAIR("doitrand_raw.sub", SUBGHZ_PROTOCOL_DOITRAND_NAME, ExpectDecode),
    PAIR("phoenix_v2_raw.sub", SUBGHZ_PROTOCOL_PHOENIX_V2_NAME, ExpectDecode),
    PAIR("honeywell_wdb_raw.sub", SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME, ExpectDecode),
    PAIR("magellan_raw.sub", SUBGHZ_PROTOCOL_MAGELLAN_NAME, ExpectDecode),
    PAIR("intertechno_v3_raw.sub", SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME, ExpectDecode),
    PAIR("clemsa_raw.sub", SUBGHZ_PROTOCOL_CLEMSA_NAME, ExpectDecode),
    PAIR("ansonic_raw.sub", SUBGHZ_PROTOCOL_ANSONIC_NAME, ExpectDecode),
    PAIR("smc5326_raw.sub", SUBGHZ_PROTOCOL_SMC5326_NAME, ExpectDecode),
    PAIR("holtek_ht12x_raw.sub", SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME, ExpectDecode),
    PAIR("dooya_raw.sub", SUBGHZ_PROTOCOL_DOOYA_NAME, ExpectDecode),
    PAIR("alutech_at_4n_raw.sub", SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME, ExpectDataOnly),
    PAIR("nice_one_raw.sub", SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME, ExpectDecode),
    PAIR("kinggates_stylo4k_raw.sub", SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME, ExpectDecode),
    PAIR("mastercode_raw.sub", SUBGHZ_PROTOCOL_MASTERCODE_NAME, ExpectDecode),
    PAIR("dickert_raw.sub", SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME, ExpectDecode),
    PAIR("roger_raw.sub", SUBGHZ_PROTOCOL_ROGER_NAME, ExpectDecode),
    PAIR("hollarm_raw.sub", SUBGHZ_PROTOCOL_HOLLARM_NAME, ExpectDecode),
    PAIR("revers_rb2_raw.sub", SUBGHZ_PROTOCOL_REVERSRB2_NAME, ExpectDecode),
    PAIR("gangqi_raw.sub", SUBGHZ_PROTOCOL_GANGQI_NAME, ExpectDecode),
    PAIR("hay21_raw.sub", SUBGHZ_PROTOCOL_HAY21_NAME, ExpectDecode),
    PAIR("feron_raw.sub", SUBGHZ_PROTOCOL_FERON_NAME, ExpectDecode),
    PAIR("legrand_raw.sub", SUBGHZ_PROTOCOL_LEGRAND_NAME, ExpectDecode),
    PAIR("marantec24_raw.sub", SUBGHZ_PROTOCOL_MARANTEC24_NAME, ExpectDecode),
};
#define PAIR_COUNT (sizeof(pairs) / sizeof(pairs[0]))

/* The firmware's whole-receiver test: this many decodes from this capture
 * (subghz_decode_random_test, TEST_RANDOM_COUNT_PARSE). It runs after the
 * decoder tests, on the receiver they used, and some decoders keep state
 * through subghz_receiver_reset: Holtek_HT12X reports a frame only when it
 * equals the one before, and the frame before is kept (last_data). Left from
 * its own decoder test, it makes the first matching frame count at once, so
 * a fresh receiver (the app's, or `subghz decode_raw`'s) makes one fewer. */
#define RANDOM_FILE        "test_random_raw.sub"
#define RANDOM_COUNT       328
#define RANDOM_COUNT_FRESH (RANDOM_COUNT - 1)

/* ---- Reading subghz_test.c ------------------------------------------------ */

static char* read_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if(!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* text = malloc((size_t)size + 1);
    size_t n = fread(text, 1, (size_t)size, fp);
    text[n] = '\0';
    fclose(fp);
    return text;
}

static const char* skip_space(const char* p) {
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/* Every `subghz_decoder_test(EXT_PATH("unit_tests/subghz/FILE"), CONSTANT)`
 * there must be one of ours, and the other way round. */
static void test_list_matches_firmware(void) {
    printf("test_list_matches_firmware\n");
    char* text = read_file(FW_TEST_C);
    CHECK(text != NULL, "the firmware's subghz_test.c is there");
    if(!text) return;
    const char* call = "subghz_decoder_test(";
    const char* dir = "EXT_PATH(\"unit_tests/subghz/";
    bool seen[PAIR_COUNT] = {false};
    size_t calls = 0, unknown = 0;
    for(const char* p = strstr(text, call); p; p = strstr(p, call)) {
        p = skip_space(p + strlen(call));
        if(strncmp(p, dir, strlen(dir)) != 0) continue; /* the definition */
        p += strlen(dir);
        const char* end = strchr(p, '"');
        if(!end) break;
        char file[64];
        snprintf(file, sizeof(file), "%.*s", (int)(end - p), p);
        p = skip_space(end + 1);
        if(*p == ')') p = skip_space(p + 1);
        if(*p == ',') p = skip_space(p + 1);
        size_t len = strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        char constant[64];
        snprintf(constant, sizeof(constant), "%.*s", (int)len, p);
        calls++;
        size_t i = 0;
        while(i < PAIR_COUNT &&
              !(strcmp(pairs[i].file, file) == 0 && strcmp(pairs[i].constant, constant) == 0))
            i++;
        if(i == PAIR_COUNT) {
            printf("  not in our list: %s -> %s\n", file, constant);
            unknown++;
        } else {
            seen[i] = true;
        }
    }
    size_t missing = 0;
    for(size_t i = 0; i < PAIR_COUNT; i++) {
        if(!seen[i]) {
            printf("  not in subghz_test.c: %s -> %s\n", pairs[i].file, pairs[i].constant);
            missing++;
        }
    }
    printf("  %zu decoder tests there, %zu here\n", calls, PAIR_COUNT);
    CHECK(calls == PAIR_COUNT && unknown == 0 && missing == 0, "same capture/protocol pairs");
    char want[64];
    snprintf(want, sizeof(want), "TEST_RANDOM_COUNT_PARSE %d", RANDOM_COUNT);
    CHECK(strstr(text, want) != NULL, "same expected count for the random capture");
    free(text);
}

/* ---- Running one capture ------------------------------------------------- */

#define MAX_PROTOCOLS 64
#define MAX_KEYS      8

typedef struct {
    char name[RG_DECODE_NAME];
    uint32_t count;
} ProtocolCount;

typedef struct {
    uint32_t bits;
    uint64_t key;
} DecodedKey;

/* Filled by the child, read by the parent (shared memory). */
typedef struct {
    bool done; /* the whole capture was fed */
    char describing[RG_DECODE_NAME]; /* protocol being described, if it stopped there */
    uint32_t fed;
    bool corrupt;
    RgDecodeLog log;
    ProtocolCount protocol[MAX_PROTOCOLS]; /* every decode, by protocol */
    size_t protocols;
    DecodedKey key[MAX_KEYS]; /* distinct keys of the expected protocol */
    size_t keys;
    bool key_unread; /* one could not be read back from its .sub form */
    uint32_t tail_decodes; /* decodes with a quiet line after the capture */
} RunResult;

typedef struct {
    RunResult* result;
    const char* expected; /* NULL: none */
    bool own_text; /* the decoder's own text, not the app's (to show why) */
    bool replay; /* first the firmware's decoder tests, on the same receiver */
    FuriString* text;
} Run;

/* rg_decode_run's feed: the app's receiver gets every sample as it comes;
 * the second receiver one sample later, so the last can become a gap. */
typedef struct {
    SubGhzReceiver* receiver;
    SubGhzReceiver* tail;
    bool held;
    bool held_level;
    uint32_t held_duration;
} Feed;

static void feed(void* ctx, bool level, uint32_t duration) {
    Feed* f = ctx;
    subghz_receiver_decode(f->receiver, level, duration);
    if(f->held) subghz_receiver_decode(f->tail, f->held_level, f->held_duration);
    f->held = true;
    f->held_level = level;
    f->held_duration = duration;
}

static void feed_tail_gap(Feed* f) {
    if(!f->held) return;
    if(f->held_level) {
        subghz_receiver_decode(f->tail, true, f->held_duration);
        subghz_receiver_decode(f->tail, false, TAIL_GAP_US);
    } else {
        subghz_receiver_decode(f->tail, false, f->held_duration + TAIL_GAP_US);
    }
}

static size_t file_read(void* ctx, uint8_t* buf, size_t len) {
    return fread(buf, 1, len, (FILE*)ctx);
}

static bool file_seek(void* ctx, uint32_t offset) {
    return fseek((FILE*)ctx, (long)offset, SEEK_SET) == 0;
}

static void count_protocol(RunResult* r, const char* name) {
    for(size_t i = 0; i < r->protocols; i++) {
        if(strcmp(r->protocol[i].name, name) == 0) {
            r->protocol[i].count++;
            return;
        }
    }
    if(r->protocols == MAX_PROTOCOLS) return;
    ProtocolCount* c = &r->protocol[r->protocols++];
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->count = 1;
}

/* The decode's Bit and Key, from its .sub form (as the app saves it). */
static void note_key(RunResult* r, SubGhzProtocolDecoderBase* decoder_base) {
    FlipperFormat* ff = flipper_format_string_alloc();
    SubGhzRadioPreset preset = {furi_string_alloc_set_str("AM650"), 433920000, NULL, 0};
    uint32_t bits = 0;
    uint8_t bytes[8];
    bool ok = subghz_protocol_decoder_base_serialize(decoder_base, ff, &preset) ==
                  SubGhzProtocolStatusOk &&
              flipper_format_rewind(ff) && flipper_format_read_uint32(ff, "Bit", &bits, 1) &&
              flipper_format_read_hex(ff, "Key", bytes, sizeof(bytes));
    flipper_format_free(ff);
    furi_string_free(preset.name);
    if(!ok) {
        r->key_unread = true;
        return;
    }
    uint64_t key = 0;
    for(size_t i = 0; i < sizeof(bytes); i++)
        key = (key << 8) | bytes[i];
    for(size_t i = 0; i < r->keys; i++) {
        if(r->key[i].bits == bits && r->key[i].key == key) return;
    }
    if(r->keys < MAX_KEYS) r->key[r->keys++] = (DecodedKey){bits, key};
}

/* As the app's: describe (with the app's radiogeddon_decode_text), hash,
 * log, reset. */
static void
    rx_callback(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder_base, void* context) {
    Run* run = context;
    RunResult* r = run->result;
    const char* name = (decoder_base->protocol && decoder_base->protocol->name) ?
                           decoder_base->protocol->name :
                           "";
    count_protocol(r, name);
    snprintf(r->describing, sizeof(r->describing), "%s", name);
    if(run->own_text) {
        furi_string_reset(run->text);
        subghz_protocol_decoder_base_get_string(decoder_base, run->text);
    } else {
        radiogeddon_decode_text(decoder_base, NULL, run->text);
    }
    if(run->expected && strcmp(name, run->expected) == 0) note_key(r, decoder_base);
    r->describing[0] = '\0';
    uint8_t hash = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    rg_decode_log_add(&r->log, name, furi_string_get_cstr(run->text), hash);
    subghz_receiver_reset(receiver);
}

static void tail_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    (void)decoder_base;
    ((RunResult*)context)->tail_decodes++;
    subghz_receiver_reset(receiver);
}

static bool find_file(const char* name, char* path, size_t cap);

static void feed_one(void* ctx, bool level, uint32_t duration) {
    SubGhzProtocolDecoderBase* decoder = ctx;
    decoder->protocol->decoder->feed(decoder, level, duration);
}

/* What subghz_test.c does before its random test, on the same receiver: its
 * decoder tests, each capture fed to its protocol's decoder alone. */
static void replay_decoder_tests(SubGhzReceiver* receiver, RunResult* r) {
    static RgRawReader reader;
    for(size_t i = 0; i < PAIR_COUNT; i++) {
        SubGhzProtocolDecoderBase* decoder =
            subghz_receiver_search_decoder_base_by_name(receiver, pairs[i].protocol);
        char path[512];
        FILE* fp = NULL;
        if(decoder && find_file(pairs[i].file, path, sizeof(path))) fp = fopen(path, "rb");
        if(!fp) continue;
        RgRawSource source = {file_read, file_seek, fp};
        rg_raw_reader_init(&reader, source);
        rg_decode_run(&reader, &r->log, feed_one, decoder, NULL, NULL);
        fclose(fp);
    }
    subghz_receiver_reset(receiver); /* as the random test starts */
    r->protocols = 0;
}

static SubGhzReceiver* receiver_alloc(SubGhzEnvironment* environment) {
    SubGhzReceiver* receiver = subghz_receiver_alloc_init(environment);
    subghz_receiver_set_filter(receiver, SubGhzProtocolFlag_Decodable);
    return receiver;
}

/* In the child: decode @p path as the app does. */
static void run_capture(const char* path, Run* run) {
    RunResult* r = run->result;
    FILE* fp = fopen(path, "rb");
    if(!fp) return;

    /* As radiogeddon_subghz_environment_acquire: the full registry, no
     * keystore loaded, no rainbow tables. */
    SubGhzEnvironment* environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(environment, (void*)&subghz_protocol_registry);
    subghz_environment_set_came_atomo_rainbow_table_file_name(environment, NULL);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(environment, NULL);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(environment, NULL);

    Feed f = {receiver_alloc(environment), receiver_alloc(environment), false, false, 0};
    subghz_receiver_set_rx_callback(f.receiver, rx_callback, run);
    subghz_receiver_set_rx_callback(f.tail, tail_callback, r);
    run->text = furi_string_alloc();
    if(run->replay) replay_decoder_tests(f.receiver, r);

    static RgRawReader reader;
    RgRawSource source = {file_read, file_seek, fp};
    rg_raw_reader_init(&reader, source);
    rg_decode_log_init(&r->log);
    r->fed = rg_decode_run(&reader, &r->log, feed, &f, NULL, NULL);
    r->corrupt = reader.corrupt;
    feed_tail_gap(&f);

    subghz_receiver_free(f.receiver);
    subghz_receiver_free(f.tail);
    subghz_environment_free(environment);
    furi_string_free(run->text);
    fclose(fp);
    r->done = true;
}

typedef enum {
    OutcomeDone,
    OutcomeCrash, /* the child stopped with an error or signal */
    OutcomeHang, /* it took longer than RUN_TIMEOUT_S */
} Outcome;

/* Run @p path in a child process; its stderr goes to CHILD_LOG. */
static Outcome run_isolated(const char* path, Run run) {
    RunResult* r = run.result;
    memset(r, 0, sizeof(*r));
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if(pid == 0) {
        if(!freopen(CHILD_LOG, "w", stderr)) _exit(2);
        alarm(RUN_TIMEOUT_S);
        run_capture(path, &run);
        exit(0); /* through LeakSanitizer's check */
    }
    int status = 0;
    if(pid < 0 || waitpid(pid, &status, 0) != pid) return OutcomeCrash;
    if(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) return OutcomeHang;
    if(!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !r->done) return OutcomeCrash;
    return OutcomeDone;
}

/* The sanitizer's findings in the child's error output. */
static void print_child_log(void) {
    FILE* fp = fopen(CHILD_LOG, "r");
    if(!fp) return;
    char line[512];
    unsigned shown = 0;
    while(fgets(line, sizeof(line), fp) && shown < 6) {
        if(strstr(line, "runtime error") || strstr(line, "ERROR") || strstr(line, "    #0 ") ||
           strstr(line, "    #1 ") || strstr(line, "not available")) {
            printf("      | %s", line);
            shown++;
        }
    }
    fclose(fp);
}

/* ---- Captures and key files ---------------------------------------------- */

/* The firmware's test names two captures with a capital letter; the card is
 * case-insensitive, the host is not. */
static bool find_file(const char* name, char* path, size_t cap) {
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

typedef struct {
    char file[64];
    uint32_t bits;
    uint64_t key;
} KeyFile;

static bool value_of(const char* line, const char* key, const char** value) {
    size_t n = strlen(key);
    if(strncmp(line, key, n) != 0 || line[n] != ':') return false;
    *value = skip_space(line + n + 1);
    return true;
}

/* Read @p path as a key file of @p protocol (not RAW, with an 8-byte Key). */
static bool read_key_file(const char* path, const char* protocol, KeyFile* k) {
    FILE* fp = fopen(path, "r");
    if(!fp) return false;
    char line[256];
    bool raw = false, same = false, have_key = false;
    while(fgets(line, sizeof(line), fp)) {
        const char* v;
        if(value_of(line, "Filetype", &v)) {
            raw = strncmp(v, "Flipper SubGhz RAW File", 23) == 0;
        } else if(value_of(line, "Protocol", &v)) {
            size_t n = strcspn(v, "\r\n");
            same = n == strlen(protocol) && strncmp(v, protocol, n) == 0;
        } else if(value_of(line, "Bit", &v)) {
            k->bits = (uint32_t)strtoul(v, NULL, 10);
        } else if(value_of(line, "Key", &v)) {
            unsigned b[8];
            have_key = sscanf(
                           v,
                           "%x %x %x %x %x %x %x %x",
                           &b[0],
                           &b[1],
                           &b[2],
                           &b[3],
                           &b[4],
                           &b[5],
                           &b[6],
                           &b[7]) == 8;
            k->key = 0;
            for(int i = 0; i < 8 && have_key; i++)
                k->key = (k->key << 8) | b[i];
        }
    }
    fclose(fp);
    return !raw && same && have_key;
}

/* The one key file in the folder for @p protocol, if there is exactly one. */
static bool find_key_file(const char* protocol, KeyFile* out) {
    DIR* d = opendir(FW_SUBGHZ);
    if(!d) return false;
    struct dirent* e;
    unsigned found = 0;
    while((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if(len < 5 || strcmp(e->d_name + len - 4, ".sub") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", FW_SUBGHZ, e->d_name);
        KeyFile k = {{0}, 0, 0};
        if(read_key_file(path, protocol, &k)) {
            snprintf(k.file, sizeof(k.file), "%s", e->d_name);
            if(!found++) *out = k;
        }
    }
    closedir(d);
    return found == 1;
}

/* ---- The table ----------------------------------------------------------- */

static uint32_t count_of(const RunResult* r, const char* protocol) {
    for(size_t i = 0; i < r->protocols; i++) {
        if(strcmp(r->protocol[i].name, protocol) == 0) return r->protocol[i].count;
    }
    return 0;
}

static const RgDecodeHit* in_log(const RgDecodeLog* log, const char* protocol) {
    for(size_t i = 0; i < log->hit_count; i++) {
        if(strcmp(log->hit[i].protocol, protocol) == 0) return &log->hit[i];
    }
    return NULL;
}

static void format_counts(const RunResult* r, char* out, size_t cap) {
    size_t len = 0;
    out[0] = '\0';
    for(size_t i = 0; i < r->protocols && len < cap; i++) {
        len += (size_t)snprintf(
            out + len,
            cap - len,
            "%s%s x%lu",
            i ? ", " : "",
            r->protocol[i].name,
            (unsigned long)r->protocol[i].count);
    }
    if(!r->protocols) snprintf(out, cap, "-");
}

/* How the tail receiver differs, when it does. */
static void print_tail(const RunResult* r) {
    long more = (long)r->tail_decodes - (long)r->log.decodes;
    if(more) printf("      with a quiet line after the capture: %+ld decodes\n", more);
}

static void print_key_file(const RunResult* r, const KeyFile* k, bool match) {
    printf(
        "      key file %s: %lu bit %llX, %s",
        k->file,
        (unsigned long)k->bits,
        (unsigned long long)k->key,
        match ? "also decoded from the capture\n" : "not decoded from it; decoded:");
    for(size_t j = 0; !match && j < r->keys; j++) {
        printf(" %lu bit %llX", (unsigned long)r->key[j].bits, (unsigned long long)r->key[j].key);
    }
    if(!match) printf("%s\n", r->key_unread ? " (some not readable)" : "");
}

typedef struct {
    unsigned decoded, data_only, failed;
    unsigned key_files, key_match, tail_differs;
} Totals;

static void test_pairs(RunResult* r, Totals* t) {
    printf("test_pairs (capture, expected protocol, result, decodes by protocol)\n");
    for(size_t i = 0; i < PAIR_COUNT; i++) {
        const Pair* p = &pairs[i];
        char path[512];
        bool found = find_file(p->file, path, sizeof(path));
        CHECK(found, "capture present");
        if(!found) continue;
        Run run = {r, p->protocol, false, false, NULL};
        Outcome outcome = run_isolated(path, run);
        uint32_t hits = count_of(r, p->protocol);
        const RgDecodeHit* hit = outcome == OutcomeDone ? in_log(&r->log, p->protocol) : NULL;
        bool decoded = hits > 0 && hit;
        bool data_only = radiogeddon_decode_text_avoids_decoder(p->protocol);
        const char* result = decoded && data_only    ? "decoded (data-only text)" :
                             decoded                 ? "decoded" :
                             outcome == OutcomeHang  ? "HUNG" :
                             outcome == OutcomeCrash ? "CRASHED" :
                                                       "NOT DECODED";
        char counts[512];
        format_counts(r, counts, sizeof(counts));
        printf("  %-26s %-18s %s: %s\n", p->file, p->protocol, result, counts);
        if(outcome == OutcomeCrash) {
            if(r->describing[0]) printf("      while describing its %s decode\n", r->describing);
            print_child_log();
        }
        if(outcome == OutcomeDone) {
            CHECK(r->fed > 0 && !r->corrupt, "the capture was read whole");
            if(r->log.dropped) {
                printf(
                    "      decodes of entries beyond the log's %d: %lu\n",
                    RG_DECODE_MAX_HITS,
                    (unsigned long)r->log.dropped);
            }
            print_tail(r);
            if(r->tail_decodes != r->log.decodes) t->tail_differs++;
        }
        CHECK(decoded, "the expected protocol decodes and is described, through the log");
        t->decoded += decoded;
        t->failed += !decoded;
        CHECK(
            data_only == (p->expect == ExpectDataOnly),
            "the app's own text only for the decoders that need a table");

        if(p->expect == ExpectDataOnly) {
            CHECK(
                hit && strncmp(hit->text, p->protocol, strlen(p->protocol)) == 0 &&
                    strstr(hit->text, "bit\nKey:") && strstr(hit->text, "rainbow"),
                "described by name, bits and key, saying why");
            if(hit) printf("      the app's text: %.60s...\n", hit->text);
            /* Why: the decoder's own text, in the app's environment. */
            Run own = {r, p->protocol, true, false, NULL};
            Outcome own_outcome = run_isolated(path, own);
            bool table_crash = own_outcome == OutcomeCrash &&
                               strcmp(r->describing, p->protocol) == 0;
            printf(
                "      its decoder's own text: %s\n",
                table_crash ? "crashes (reads the NULL rainbow table name)" : "does not crash");
            CHECK(table_crash, "the decoder's own text still reads the NULL table name");
            t->data_only += decoded && table_crash;
        }

        KeyFile k;
        if(decoded && find_key_file(p->protocol, &k)) {
            bool match = false;
            for(size_t j = 0; j < r->keys; j++) {
                if(r->key[j].bits == k.bits && r->key[j].key == k.key) match = true;
            }
            t->key_files++;
            t->key_match += match;
            print_key_file(r, &k, match);
        }
    }
}

/* The firmware's whole-receiver capture: every decode counts. Its test
 * describes them with the rainbow tables present; here every decode is
 * described with the app's text, as the app has none. Run fresh, as the app
 * would, and after replaying the decoder tests, as the firmware's suite
 * does. */
static void test_random_capture(RunResult* r) {
    printf("test_random_capture\n");
    char path[512];
    bool found = find_file(RANDOM_FILE, path, sizeof(path));
    CHECK(found, "capture present");
    if(!found) return;
    for(int replay = 0; replay < 2; replay++) {
        Run run = {r, NULL, false, replay, NULL};
        Outcome outcome = run_isolated(path, run);
        int want = replay ? RANDOM_COUNT : RANDOM_COUNT_FRESH;
        char counts[1024];
        format_counts(r, counts, sizeof(counts));
        printf(
            "  %s, %s: %lu decodes (want %d)%s%s\n",
            RANDOM_FILE,
            replay ? "after the firmware's decoder tests" : "fresh receiver",
            (unsigned long)r->log.decodes,
            want,
            replay ? "" : ": ",
            replay ? "" : counts);
        if(outcome == OutcomeCrash) print_child_log();
        CHECK(outcome == OutcomeDone, "the random capture runs through");
        CHECK(r->log.decodes == (uint32_t)want, "as many decodes as the firmware's test");
        if(outcome == OutcomeDone && !replay) print_tail(r);
    }
}

int main(void) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    test_list_matches_firmware();
    RunResult* r =
        mmap(NULL, sizeof(*r), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK(r != MAP_FAILED, "shared memory for the children");
    Totals t = {0};
    if(r != MAP_FAILED) {
        test_pairs(r, &t);
        test_random_capture(r);
        munmap(r, sizeof(*r));
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf(
        "\n%zu captures: %u decoded as expected (%u with the app's data-only text, whose "
        "decoder's own text crashes without a rainbow table), %u otherwise\n",
        PAIR_COUNT,
        t.decoded,
        t.data_only,
        t.failed);
    printf(
        "key files for the same protocol: %u, their key also decoded from the capture: %u\n",
        t.key_files,
        t.key_match);
    printf("captures where a quiet line after the end changes the count: %u\n", t.tail_differs);
    printf("%d checks, %d failures (%.1f s)\n", g_checks, g_failures, secs);
    if(g_failures) {
        printf("FIRMWARE DECODER TESTS FAILED\n");
        return 1;
    }
    printf("ALL FIRMWARE DECODER TESTS PASSED\n");
    return 0;
}
