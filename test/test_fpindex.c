/**
 * Host tests for the fingerprint index and Find Similar Signals
 * (helpers/radiogeddon_fp.c) on a real folder through the stub Furi/Storage
 * layer (EXT_PATH maps to test/build/ext), with the firmware's own
 * FlipperFormat code parsing the recordings, as on the device.
 *
 * Covers: similar recordings ranked first, the index reused on the next
 * search (nothing recomputed), invalidation when a recording changes, is
 * renamed or deleted, damaged and foreign index files, a write failure
 * (the old index kept, results still given), low memory (refused before
 * any allocation), a large collection, and that recordings are never
 * written and every byte and file is released. All recordings are
 * synthetic; build and run with `make -C test fpindex`.
 */
#include "furi.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_fp.h"
#include "../helpers/radiogeddon_storage.h"

#include <sys/stat.h>
#include <sys/time.h>
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

extern size_t stub_heap_free;

static int g_card;
static Storage* const CARD = (Storage*)&g_card; // never dereferenced by the stub
static RadioGeddonFpResult g_res;
static uint32_t g_window = 0; // the search's time window (0: all)

static void path_of(const char* name, char* out, size_t size) {
    snprintf(out, size, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
}

/* A RAW recording: @p frames repeats of a PWM frame of @p bits at base
 * time @p te, on @p hz. */
static void write_raw(const char* name, const char* bits, int te, int frames, uint32_t hz) {
    char path[256];
    path_of(name, path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: -20000",
        (unsigned long)hz);
    int col = 1;
    for(int f = 0; f < frames; f++) {
        for(const char* b = bits; *b; b++) {
            int hi = *b == '1' ? 3 * te : te, lo = *b == '1' ? te : 3 * te;
            fprintf(fp, " %d %d", hi, -lo);
            col += 2;
            if(col > 500) {
                fprintf(fp, "\nRAW_Data:");
                col = 0;
            }
        }
        fprintf(fp, " %d %d", te, -31 * te);
        col += 2;
    }
    fprintf(fp, "\n");
    fclose(fp);
}

static void write_noise(const char* name) {
    char path[256];
    path_of(name, path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data:");
    uint32_t r = 7;
    for(int i = 0; i < 1500; i++) {
        r = r * 1103515245u + 12345u;
        fprintf(fp, " %d", (i % 2 ? -1 : 1) * (int)(40 + (r >> 16) % 900));
        if(i % 400 == 399) fprintf(fp, "\nRAW_Data:");
    }
    fprintf(fp, "\n");
    fclose(fp);
}

static void write_decoded(const char* name, const char* protocol, int bits, const char* key) {
    char path[256];
    path_of(name, path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: %s\nBit: %d\nKey: %s\n",
        protocol,
        bits,
        key);
    fclose(fp);
}

static void reset_card(void) {
    if(system("rm -rf build/ext && mkdir -p build/ext/apps_data/radiogeddon/signals") != 0) {
        printf("cannot reset build/ext\n");
        exit(2);
    }
}

static int hit_index(const char* name) {
    for(size_t i = 0; i < g_res.count; i++)
        if(strcmp(g_res.hit[i].name, name) == 0) return (int)i;
    return -1;
}

static bool search(const char* target) {
    char path[256];
    path_of(target, path, sizeof(path));
    return radiogeddon_fp_find_similar(CARD, path, g_window, &g_res, NULL, NULL);
}

static const char* KEY_A = "101100111000101011110001";
static const char* KEY_B = "010011000111010100001110";

static void test_ranking(void) {
    printf("test_ranking\n");
    reset_card();
    write_raw("remote_a1.sub", KEY_A, 350, 6, 433920000);
    write_raw("remote_a2.sub", KEY_A, 352, 9, 433920000); // same frame, more repeats
    write_raw("remote_b.sub", KEY_B, 350, 6, 433920000); // other button
    write_raw("other_te.sub", KEY_A, 900, 6, 433920000); // other timing
    write_noise("noise.sub");
    write_decoded("came.sub", "CAME", 12, "00 00 00 00 00 00 0A BC");
    size_t bytes = stub_live_bytes, blocks = stub_live_blocks;
    CHECK(search("remote_a1.sub"), "search ran");
    CHECK(g_res.target_found && g_res.target_structure, "target fingerprinted");
    CHECK(g_res.computed == 6 && g_res.indexed == 6, "every recording indexed");
    CHECK(g_res.count >= 2, "matches found");
    CHECK(hit_index("remote_a2.sub") == 0, "the same frame ranks first");
    CHECK(g_res.hit[0].match.score >= 85, "strong");
    CHECK(g_res.hit[0].match.evidence & RgFpEvPattern, "the same frame is the evidence");
    int b = hit_index("remote_b.sub");
    CHECK(b > 0, "the other button follows");
    CHECK(hit_index("noise.sub") < 0, "noise is not similar");
    int o = hit_index("other_te.sub");
    CHECK(o < 0 || g_res.hit[o].match.score < g_res.hit[b].match.score, "other Te ranks lower");
    char text[400];
    size_t n = radiogeddon_fp_explain(&g_res, 0, text, sizeof(text));
    CHECK(n > 0 && strstr(text, "remote_a2.sub") && strstr(text, "not proof"), "explained");
    CHECK(radiogeddon_fp_explain(&g_res, 99, text, sizeof(text)) == 0, "no such hit");
    CHECK(stub_files_open == 0, "every file closed");
    CHECK(stub_live_bytes == bytes && stub_live_blocks == blocks, "every byte released");

    // The next search reuses the index: nothing is recomputed.
    CHECK(search("remote_b.sub"), "second search");
    CHECK(g_res.computed == 0 && g_res.indexed == 6 && g_res.dropped == 0, "index reused");
    CHECK(hit_index("remote_a1.sub") >= 0, "and finds the other button's remote");
}

static void test_invalidation(void) {
    printf("test_invalidation\n");
    // A changed recording (size and time differ) is fingerprinted again.
    write_raw("remote_b.sub", KEY_A, 350, 7, 433920000);
    struct timespec ts = {1, 100000000};
    nanosleep(&ts, NULL);
    CHECK(search("remote_a1.sub"), "search");
    CHECK(g_res.computed == 1 && g_res.dropped == 1, "only the changed one recomputed");
    CHECK(
        hit_index("remote_b.sub") >= 0 &&
            g_res.hit[hit_index("remote_b.sub")].match.evidence & RgFpEvPattern,
        "its new content is what is compared");
    // Renamed and deleted recordings.
    char from[256], to[256];
    path_of("remote_a2.sub", from, sizeof(from));
    path_of("renamed.sub", to, sizeof(to));
    rename(from, to);
    path_of("came.sub", from, sizeof(from));
    remove(from);
    CHECK(search("remote_a1.sub"), "search");
    CHECK(g_res.indexed == 5, "deleted one gone, renamed one indexed");
    // (remote_b.sub now holds the same frame too: either may lead.)
    int rn = hit_index("renamed.sub");
    CHECK(
        hit_index("remote_a2.sub") < 0 && rn >= 0 && rn <= 1 &&
            (g_res.hit[rn].match.evidence & RgFpEvPattern),
        "under its new name");
    // The recordings were never written.
    FILE* fp = fopen(to, "rb");
    char head[40] = {0};
    CHECK(
        fp && fread(head, 1, 39, fp) == 39 &&
            strncmp(head, "Filetype: Flipper SubGhz RAW", 28) == 0,
        "recording untouched");
    if(fp) fclose(fp);
    // A target that is not in the index any more is fingerprinted directly.
    CHECK(search("missing.sub") && !g_res.target_found, "missing target");
}

static void test_damage(void) {
    printf("test_damage\n");
    // Flip a byte inside the second record: that record is dropped (and
    // rebuilt from its recording), the others survive.
    FILE* fp = fopen(RADIOGEDDON_FP_INDEX, "r+b");
    fseek(fp, RG_FP_HEADER_BYTES + RG_FP_RECORD_BYTES + 70, SEEK_SET);
    int c = fgetc(fp);
    fseek(fp, -1, SEEK_CUR);
    fputc(c ^ 0x40, fp);
    fclose(fp);
    CHECK(search("remote_a1.sub"), "search");
    CHECK(g_res.dropped == 1 && g_res.computed == 1 && g_res.indexed == 5, "one record rebuilt");
    // Garbage instead of an index: rebuilt from scratch.
    fp = fopen(RADIOGEDDON_FP_INDEX, "wb");
    fputs("this is not an index", fp);
    fclose(fp);
    CHECK(search("remote_a1.sub"), "search");
    CHECK(g_res.computed == 5 && g_res.indexed == 5 && hit_index("renamed.sub") >= 0, "rebuilt");
    // A truncated index (cut mid-record) keeps the whole records.
    struct stat st;
    stat(RADIOGEDDON_FP_INDEX, &st);
    CHECK(truncate(RADIOGEDDON_FP_INDEX, st.st_size - 50) == 0, "truncated");
    CHECK(search("remote_a1.sub"), "search");
    CHECK(g_res.computed == 1 && g_res.indexed == 5, "only the cut record rebuilt");
}

static void test_write_failure_and_memory(void) {
    printf("test_write_failure_and_memory\n");
    struct stat before, after;
    stat(RADIOGEDDON_FP_INDEX, &before);
    write_raw("new_one.sub", KEY_B, 350, 5, 433920000);
    stub_write_fail_after = 100; // the card fills during the rewrite
    CHECK(search("remote_a1.sub"), "search still answers");
    stub_write_fail_after = -1;
    CHECK(g_res.index_failed, "the failure is reported");
    stat(RADIOGEDDON_FP_INDEX, &after);
    CHECK(before.st_size == after.st_size, "the old index is kept whole");
    CHECK(access(RADIOGEDDON_FP_TEMP, F_OK) != 0, "no temporary file left");
    CHECK(g_res.target_found, "the target is still fingerprinted");
    CHECK(stub_files_open == 0, "files closed after a failure");
    // Low memory: refused before anything is allocated.
    size_t heap = stub_heap_free;
    stub_heap_free = radiogeddon_fp_memory() - 1;
    size_t bytes = stub_live_bytes;
    CHECK(!search("remote_a1.sub"), "refused");
    CHECK(stub_live_bytes == bytes, "nothing allocated");
    stub_heap_free = heap;
    CHECK(search("remote_a1.sub") && !g_res.index_failed && g_res.indexed == 6, "then works");
}

static void test_large_collection(void) {
    printf("test_large_collection\n");
    reset_card();
    char name[32];
    for(int i = 0; i < 120; i++) {
        snprintf(name, sizeof(name), "cap_%03d.sub", i);
        write_raw(name, (i % 3) ? KEY_A : KEY_B, 300 + (i % 7) * 60, 3, 433920000);
    }
    CHECK(search("cap_000.sub"), "search over 120");
    CHECK(g_res.indexed == 120 && g_res.compared == 119, "every recording ranked");
    CHECK(g_res.count == RG_FP_TOP, "the best kept");
    bool ordered = true;
    for(size_t i = 1; i < g_res.count; i++)
        if(g_res.hit[i].match.score > g_res.hit[i - 1].match.score) ordered = false;
    CHECK(ordered, "best first");
    CHECK(g_res.hit[0].match.evidence & RgFpEvPattern, "an identical frame leads");
    CHECK(stub_files_open == 0, "files closed");
}

/* A decoded key file: cheap to fingerprint, so a big library stays quick. */
static void write_key(const char* name, unsigned key) {
    char path[256];
    path_of(name, path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: Princeton\nBit: 24\n"
        "Key: 00 00 00 00 00 %02X %02X %02X\nTE: 400\n",
        (key >> 16) & 0xFF,
        (key >> 8) & 0xFF,
        key & 0xFF);
    fclose(fp);
}

/* Beyond beta 9's 500-record index, and with too little heap for all. */
static void test_beyond_500(void) {
    printf("test_beyond_500\n");
    reset_card();
    char name[32];
    for(int i = 0; i < 700; i++) {
        snprintf(name, sizeof(name), "k_%04d.sub", i);
        write_key(name, (unsigned)i * 2654435761u);
    }
    CHECK(search("k_0001.sub"), "search over 700");
    CHECK(g_res.indexed == 700 && !g_res.truncated, "all 700 indexed (beta 9: the first 500)");
    CHECK(g_res.compared == 699, "every other recording ranked");
    // Nothing new: one listing, nothing recomputed.
    memset(&stub_io, 0, sizeof(stub_io));
    CHECK(search("k_0001.sub") && g_res.computed == 0, "second search reuses the index");
    CHECK(stub_io.dir_opens == 1, "one folder listing when nothing is new");
    // A new recording is found with one more listing, never a full rebuild.
    write_key("k_new.sub", 12345);
    memset(&stub_io, 0, sizeof(stub_io));
    CHECK(search("k_0001.sub") && g_res.computed == 1 && g_res.indexed == 701, "one added");
    CHECK(stub_io.dir_opens == 2, "two listings for one new recording");
    // A short heap: the index is limited to what fits, never refused.
    size_t heap = stub_heap_free;
    stub_heap_free = radiogeddon_fp_memory() + 2048 + 300 * 8;
    CHECK(search("k_0001.sub"), "search with a short heap");
#ifdef RG_FP_HAS_LIMIT
    CHECK(g_res.truncated && g_res.limit < 701 && g_res.indexed == g_res.limit, "the first ones");
    CHECK(g_res.limit >= 250, "as many as fit");
#else
    CHECK(false, "a short heap limits the index (beta 9: refused)");
#endif
    stub_heap_free = heap;
    CHECK(search("k_0001.sub") && g_res.indexed == 701 && !g_res.truncated, "all again");
    CHECK(stub_files_open == 0, "files closed");
}

/* Two names whose 32-bit FNV-1a hashes are equal (beta 9 kept the second
 * out of the index): both are fingerprinted now. */
static void test_hash_collision(void) {
    printf("test_hash_collision\n");
    reset_card();
    // Found by a search over random names (FNV-1a 32 of both: 0x0a29967e).
    const char* a = "rc967e6bc.sub";
    const char* b = "r01a3505d.sub";
    uint32_t ha = 2166136261u, hb = 2166136261u;
    for(const char* p = a; *p; p++)
        ha = (ha ^ (uint8_t)*p) * 16777619u;
    for(const char* p = b; *p; p++)
        hb = (hb ^ (uint8_t)*p) * 16777619u;
    CHECK(ha == hb, "the pair collides in 32 bits");
    write_key(a, 0x123456);
    write_key(b, 0x654321);
    CHECK(search(a), "search");
    CHECK(g_res.indexed == 2 && g_res.compared == 1, "both names indexed");
    // The next search keeps both records (beta 9 dropped the second as a
    // duplicate of the first, then never added it back).
    CHECK(search(a), "search again");
    CHECK(g_res.indexed == 2 && g_res.compared == 1 && g_res.computed == 0, "both kept");
    printf("  %s and %s share a 32-bit hash\n", a, b);
}

/* Find Similar within a time window of the target's recording time. */
static void test_time_window(void) {
    printf("test_time_window\n");
    reset_card();
    char name[32], path[256];
    // Six copies of one remote, saved 0, 10 min, 50 min, 2 h, 20 h, 3 days apart.
    static const long offs[] = {0, 600, 3000, 7200, 72000, 259200};
    for(int i = 0; i < 6; i++) {
        snprintf(name, sizeof(name), "w_%d.sub", i);
        write_raw(name, KEY_A, 350, 5, 433920000);
        path_of(name, path, sizeof(path));
        struct timeval tv[2] = {{1760000000 + offs[i], 0}, {1760000000 + offs[i], 0}};
        CHECK(utimes(path, tv) == 0, "time set");
    }
    g_window = 0;
    CHECK(search("w_0.sub") && g_res.compared == 5 && g_res.out_of_scope == 0, "all: 5 ranked");
    g_window = 3600;
    memset(&stub_io, 0, sizeof(stub_io));
    CHECK(search("w_0.sub") && g_res.computed == 0, "the index is reused");
    CHECK(g_res.compared == 2 && g_res.out_of_scope == 3, "within an hour: 2 (10 and 50 min)");
    for(size_t i = 0; i < g_res.count; i++)
        CHECK(strcmp(g_res.hit[i].name, "w_1.sub") == 0 || strcmp(g_res.hit[i].name, "w_2.sub") == 0,
              "only those in the window");
    g_window = 86400;
    CHECK(search("w_0.sub") && g_res.compared == 4 && g_res.out_of_scope == 1, "within a day: 4");
    g_window = 0;
}

int main(void) {
    stub_write_fail_after = -1;
    test_ranking();
    test_invalidation();
    test_damage();
    test_write_failure_and_memory();
    test_large_collection();
    test_beyond_500();
    test_hash_collision();
    test_time_window();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
