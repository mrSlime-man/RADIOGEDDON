/*
 * Memory benchmark and stress test on the heap model (stubs/heap4.h): the
 * app's heavy operations run on recordings in a stub SD card, every
 * allocation of RadioGeddon's code, the stub strings and the firmware's own
 * FlipperFormat and stream code going to a model of the firmware's heap_4
 * allocator. Built from the same source against beta 10 and beta 11 (the
 * feature macros below pick each one's API), so both are measured alike.
 *
 *   make -C test benchmem          the tables
 *   make -C test memstress         the same with every check enforced (CI)
 *
 * Measured per operation: peak heap in use (block sizes with headers),
 * what stays allocated while its screen is open, the largest free block and
 * the number of free fragments after it, allocation calls, and whether the
 * heap is back to where it was afterwards. Then:
 *   - a low-memory sweep: the largest free block is cut in 512-byte steps;
 *     at every step an operation either completes or refuses, and no
 *     allocation may fail (on a Flipper a failed allocation stops the app),
 *     nothing may stay allocated after a refusal;
 *   - a long session: thousands of operations in random order between
 *     long-lived blocks of a simulated rest of the app, checking that no
 *     allocation fails, nothing leaks and the free space does not wear down
 *     into fragments.
 *
 * Host sizes (64-bit pointers): absolute figures are larger than a Flipper's;
 * the differences between versions are what this compares.
 */
#include "furi.h"
#include "storage/storage.h"
#include "stubs/heap4.h"
#include "../helpers/radiogeddon_analysis.h"
#include "../helpers/radiogeddon_db.h"
#include "../helpers/radiogeddon_fp.h"
#include "../helpers/radiogeddon_storage.h"

#include <sys/stat.h>

#ifdef RADIOGEDDON_ANALYSIS_MEMORY
#define VERSION "beta 11"
#else
#define VERSION "beta 10"
#endif

extern size_t stub_heap_free;

static int g_card;
static Storage* const CARD = (Storage*)&g_card;
static bool g_strict;
static int g_failures;

#define ARENA (192u * 1024u)

#define CHECK(c, ...)                         \
    do {                                      \
        if(!(c)) {                            \
            g_failures++;                     \
            printf("  FAIL: " __VA_ARGS__);   \
            printf(" (%s:%d)\n", __FILE__, __LINE__); \
        }                                     \
    } while(0)

/* ---- recordings ---------------------------------------------------------------- */

static void write_raw(int i, int frames) {
    char path[256];
    snprintf(path, sizeof(path), "%s/rec_%05d.sub", RADIOGEDDON_SIGNALS_FOLDER, i);
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: -20000");
    int col = 1;
    for(int f = 0; f < frames; f++) {
        for(int b = 0; b < 24; b++) {
            int one = ((i >> (b % 10)) ^ b) & 1, te = 300 + (i % 7) * 40;
            fprintf(fp, " %d %d", one ? 3 * te : te, one ? -te : -3 * te);
            if(++col % 256 == 0) fprintf(fp, "\nRAW_Data:");
        }
        fprintf(fp, " 300 -9300");
    }
    fprintf(fp, "\n");
    fclose(fp);
}

static void write_key(int i) {
    char path[256];
    snprintf(path, sizeof(path), "%s/rec_%05d.sub", RADIOGEDDON_SIGNALS_FOLDER, i);
    FILE* fp = fopen(path, "wb");
    fprintf(
        fp,
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: %u\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: %s\nBit: 24\n"
        "Key: 00 00 00 00 00 %02X %02X %02X\nTE: 400\n",
        433920000u + (unsigned)(i % 3) * 1000000u,
        (i % 4) ? "Princeton" : "CAME",
        (unsigned)(i * 37) & 0xFF,
        (unsigned)(i * 11) & 0xFF,
        (unsigned)i & 0xFF);
    fclose(fp);
}

/* A library of @p n recordings, one in five RAW. */
static void library(int n) {
    if(system("rm -rf build/ext && mkdir -p " RADIOGEDDON_SIGNALS_FOLDER) != 0) exit(2);
    for(int i = 0; i < n; i++)
        (i % 5 == 0) ? write_raw(i, 6) : write_key(i);
}

static void capture_path(char* out, size_t size) {
    snprintf(out, size, "%s/rec_%05d.sub", RADIOGEDDON_SIGNALS_FOLDER, 0);
}

/* ---- operations ---------------------------------------------------------------- */

/* Each opens what its screen holds (returned in *held, to close), or
 * refuses; true when it ran. */
typedef bool (*Op)(void** held);

/* Bitstream Explorer: the analysis kept while the explorer is open. */
static bool op_bitstream(void** held) {
    char path[256];
    capture_path(path, sizeof(path));
    RadioGeddonAnalysisStatus st;
#ifdef RADIOGEDDON_ANALYSIS_MEMORY
    *held = radiogeddon_analysis_run_file(CARD, path, NULL, &st);
#else
    *held = radiogeddon_analysis_run_file(CARD, path, &st);
#endif
    return *held != NULL;
}

/* Pulse Studio / Timeline: the file stays open (its reader seeks frames)
 * with the analysis, until the view is allocated. */
typedef struct {
    RadioGeddonRawFile* file;
    void* analysis;
} StudioHeld;

static bool op_studio(void** held) {
    char path[256];
    capture_path(path, sizeof(path));
    StudioHeld* s = malloc(sizeof(StudioHeld));
    s->file = radiogeddon_storage_raw_open(CARD, path);
    RadioGeddonAnalysisStatus st;
#ifdef RADIOGEDDON_ANALYSIS_MEMORY
    s->analysis = radiogeddon_analysis_run_raw(s->file, NULL, NULL, &st);
#else
    s->analysis = radiogeddon_analysis_run_raw(s->file, &st);
#endif
    if(!s->analysis) {
        radiogeddon_storage_raw_close(s->file);
        free(s);
        *held = NULL;
        return false;
    }
    *held = s;
    return true;
}

static void studio_close(void* held) {
    StudioHeld* s = held;
    free(s->analysis);
    radiogeddon_storage_raw_close(s->file);
    free(s);
}

static RadioGeddonFpResult g_fp;
static bool op_similar(void** held) {
    char path[256];
    capture_path(path, sizeof(path));
    *held = NULL;
    return radiogeddon_fp_find_similar(CARD, path, 0, NULL, &g_fp, NULL, NULL);
}

static size_t g_db_indexed;
static bool op_database(void** held) {
    RadioGeddonDbStatus st;
    RadioGeddonDb* db = radiogeddon_db_load(CARD, &st, NULL, NULL);
    g_db_indexed = db ? db->db.count : 0;
    *held = db;
    return db != NULL;
}

static void op_close(Op op, void* held) {
    if(!held) return;
    if(op == op_studio)
        studio_close(held);
    else if(op == op_database)
        radiogeddon_db_free(held);
    else
        free(held);
}

typedef struct {
    const char* name;
    Op op;
    unsigned work_kb; /* what the app checks is free before (its module's work) */
} OpInfo;

/* The tools are modules: before one runs, the app checks this much work
 * heap is free besides the module (scenes/radiogeddon_scene_tools.c; the
 * module itself is not loaded on the host). The Database is resident and
 * checks its own budget. */
static const OpInfo k_ops[] = {
    {"Bitstream Explorer", op_bitstream, 15},
    {"Pulse Studio (file + analysis)", op_studio, 20},
    {"Find Similar", op_similar, 16},
    {"Database", op_database, 0},
};

/* ---- one run, measured -------------------------------------------------------- */

typedef struct {
    bool ran;
    size_t peak; /* above the heap in use before */
    size_t held; /* still allocated while the screen is open */
    size_t max_block_open; /* largest free block while open */
    size_t fragments_open;
    uint32_t allocs;
    bool restored; /* the heap is exactly as before once closed */
} Run;

static Run measure(Op op) {
    Run r = {0};
    Heap4Stats before, open, after;
    heap4_reset_marks();
    heap4_stats(&before);
    void* held = NULL;
    r.ran = op(&held);
    heap4_stats(&open);
    r.peak = open.peak_used_bytes - before.used_bytes;
    r.held = open.used_bytes - before.used_bytes;
    r.max_block_open = open.max_free_block;
    r.fragments_open = open.free_blocks;
    r.allocs = open.allocs;
    op_close(op, held);
    heap4_stats(&after);
    r.restored = after.used_bytes == before.used_bytes &&
                 after.max_free_block == before.max_free_block &&
                 after.free_blocks == before.free_blocks;
    CHECK(after.failures == 0, "an allocation failed");
    return r;
}

static void table(const char* title, int library_size) {
    library(library_size);
    printf("\n%s (%s, %d recordings)\n", title, VERSION, library_size);
    printf(
        "%-32s %8s %8s %10s %6s %7s %s\n",
        "operation",
        "peak B",
        "held B",
        "max blk",
        "frags",
        "allocs",
        "restored");
    for(size_t i = 0; i < sizeof(k_ops) / sizeof(k_ops[0]); i++) {
        heap4_init(ARENA);
        // Index first (cold), then the warm search measured.
        if(k_ops[i].op == op_similar) {
            void* h;
            op_similar(&h);
            heap4_init(ARENA);
        }
        Run r = measure(k_ops[i].op);
        printf(
            "%-32s %8zu %8zu %10zu %6zu %7u %s\n",
            k_ops[i].name,
            r.peak,
            r.held,
            r.max_block_open,
            r.fragments_open,
            (unsigned)r.allocs,
            r.ran ? (r.restored ? "yes" : "NO") : "refused");
        if(k_ops[i].op == op_database) printf("%-32s %zu files indexed\n", "", g_db_indexed);
        CHECK(!r.ran || r.restored, "%s: heap not restored", k_ops[i].name);
    }
}

/* Smallest largest-free-block (in 512-byte steps) each operation completes
 * with; below it, it must refuse without a failed allocation or a leak. */
static void sweep(int library_size) {
    library(library_size);
    printf("\nLow memory (%s, %d recordings): smallest free block each completes with\n",
           VERSION, library_size);
    for(size_t i = 0; i < sizeof(k_ops) / sizeof(k_ops[0]); i++) {
        size_t smallest = 0;
        unsigned refused = 0, steps = 0;
        for(size_t want = 96u * 1024u; want >= 1024u; want -= 512u) {
            heap4_init(ARENA);
            if(k_ops[i].op == op_similar) { // indexed already: a warm search
                void* h;
                op_similar(&h);
                heap4_init(ARENA);
            }
            // Take all but `want` bytes of the arena in one block at the top.
            size_t take = heap4_max_free_block() - want - HEAP4_HEADER;
            void* pin = heap4_malloc(take);
            Heap4Stats before, after;
            heap4_stats(&before);
            void* held = NULL;
            bool ran = heap4_max_free_block() >= k_ops[i].work_kb * 1024u + 4096u &&
                       k_ops[i].op(&held);
            op_close(k_ops[i].op, held);
            heap4_stats(&after);
            steps++;
            CHECK(after.failures == 0, "%s with %zu B free: an allocation failed", k_ops[i].name,
                  want);
            CHECK(after.used_bytes == before.used_bytes, "%s with %zu B free: leaked %ld B",
                  k_ops[i].name, want, (long)(after.used_bytes - before.used_bytes));
            if(ran)
                smallest = want;
            else
                refused++;
            heap4_free(pin);
        }
        printf("  %-32s %7zu B  (%u of %u steps refused, no failed allocation)\n", k_ops[i].name,
               smallest, refused, steps);
    }
}

/* ---- a long session ------------------------------------------------------------ */

static uint32_t g_rng = 12345;
static uint32_t rnd(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}

static void session(int iterations) {
    library(200);
    heap4_init(ARENA);
    void* h;
    op_similar(&h); // index once
    heap4_init(ARENA);
    Heap4Stats start;
    heap4_stats(&start);
    // The rest of the app: blocks that come and go across operations.
    void* rest[64] = {0};
    size_t min_block = SIZE_MAX, max_frags = 0;
    unsigned ran = 0, refused = 0;
    for(int it = 0; it < iterations; it++) {
        size_t k = rnd() % 64;
        if(rest[k]) {
            heap4_free(rest[k]);
            rest[k] = NULL;
        } else if(heap4_max_free_block() > 40u * 1024u) {
            rest[k] = heap4_malloc(16 + rnd() % 600);
        }
        const OpInfo* o = &k_ops[rnd() % (sizeof(k_ops) / sizeof(k_ops[0]))];
        void* held = NULL;
        if(heap4_max_free_block() >= o->work_kb * 1024u + 4096u && o->op(&held))
            ran++;
        else
            refused++;
        Heap4Stats s;
        heap4_stats(&s);
        if(s.free_blocks > max_frags) max_frags = s.free_blocks;
        op_close(o->op, held);
        heap4_stats(&s);
        if(s.max_free_block < min_block) min_block = s.max_free_block;
    }
    for(size_t k = 0; k < 64; k++)
        heap4_free(rest[k]);
    Heap4Stats end;
    heap4_stats(&end);
    printf(
        "\nLong session (%s): %d operations (%u ran, %u refused) between blocks of the rest "
        "of the app\n  failed allocations %u, left allocated %zu B, largest free block "
        "%zu B at the start, %zu B at the worst, %zu B at the end, at most %zu fragments\n",
        VERSION,
        iterations,
        ran,
        refused,
        (unsigned)end.failures,
        end.used_bytes - start.used_bytes,
        start.max_free_block,
        min_block,
        end.max_free_block,
        max_frags);
    CHECK(end.failures == 0, "failed allocations in the session");
    CHECK(end.used_bytes == start.used_bytes, "memory left allocated after the session");
    CHECK(end.max_free_block == start.max_free_block, "the free space did not join up again");
    CHECK(heap4_check(), "free list inconsistent");
}

int main(int argc, char** argv) {
    g_strict = argc > 1 && strcmp(argv[1], "--check") == 0;
    stub_write_fail_after = -1;
    printf("Memory on the heap model, %s (host sizes: RgAnalysis %zu B", VERSION, sizeof(RgAnalysis));
#ifdef RADIOGEDDON_ANALYSIS_MEMORY
    printf(", analyzer working state %zu B)\n", sizeof(RgAnalyzer));
#else
    printf(", analyzer with its result %zu B)\n", sizeof(RgAnalyzer));
#endif
    table("Operations", 100);
    table("Operations", 1000);
    sweep(100);
    session(g_strict ? 1500 : 4000);
    if(system("rm -rf build/ext") != 0) return 2;
    printf("\n%s\n", g_failures ? "FAILURES" : "all memory checks passed");
    return g_failures ? 1 : 0;
}
