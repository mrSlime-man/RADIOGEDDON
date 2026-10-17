/*
 * The app's memory-critical flows, replayed on the model of the firmware's
 * allocator (stubs/heap4.h) with the real built files:
 *
 *   make -C test benchflows FLOWS_NEW="<.fap> <.fal folder>" \
 *       [FLOWS_OLD="<beta 12 .fap> <its .fal folder>"]
 *
 *   A. The RF Activity Logger after the Adaptive Scanner, step by step, as
 *      the owner's device logged it on beta 12 (Adaptive Scanner started,
 *      module unloaded, Scanner stopped, then "radiogeddon_log.fal needs
 *      17088, 13024 free"): beta 12's order (the scanner made while the
 *      setup's module is loaded) against beta 13's (the module unloaded
 *      first, radiogeddon_scene_scanner.c), from the same heap.
 *   B. Adaptive Scanner -> Back -> Logger -> Back, 2,000 times, with the
 *      GUI's small blocks coming and going in between, in both orders.
 *   C. A receive session under a constrained, fragmented heap: beta 12's
 *      decoders (BinRAW's included) against beta 13's (only those the
 *      filter feeds, radiogeddon_decoders.h), and whether the app's
 *      preflight (the measured cost plus 6 KB) ever lets a session start
 *      that then runs out (on the device: "out of memory", a crash).
 *   D. Launching, using and closing the app 60 times in a row while the
 *      firmware's services keep a few small blocks each time, and after
 *      another app ran in between, for each .fap.
 *
 * Every allocation is the device's size: the loader's from the files
 * (as in bench_launch.c and bench_modules.c), the app's from
 * scripts/struct_sizes.py and the beta 12 device log, the firmware's from
 * its source (RogueMaster 4f2e06200: lib/subghz, furi/core). A model of the
 * allocator, not a measurement on a Flipper; the device numbers of A are
 * printed next to the model's so the model can be judged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stubs/heap4.h"
#include "../helpers/rg_elf.h"

#define ARENA         (180u * 1024u)
#define MAX_BLOCKS    160u
#define SECTION_CHECK 1024u
#define MOD_LOADER    (2u * 1024u) /* radiogeddon_module.c RADIOGEDDON_MODULE_LOADER */
#define MOD_MARGIN    (4u * 1024u) /* RADIOGEDDON_MODULE_MARGIN */
#define RADIO_MARGIN  (6u * 1024u) /* RADIOGEDDON_RADIO_MARGIN */

/* ---- the loader's allocations for a .fap or a .fal ------------------------ */

typedef struct {
    uint32_t size[MAX_BLOCKS];
    uint8_t temporary[MAX_BLOCKS];
    uint8_t checked[MAX_BLOCKS];
    size_t count;
    RgElfCost cost;
    bool ok;
} Image;

typedef struct {
    void* p[MAX_BLOCKS];
    bool loaded;
} Mapped;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool file_read(void* context, uint32_t offset, void* buf, size_t len) {
    FILE* f = context;
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static void push(Image* im, uint32_t size, bool temporary, bool checked) {
    if(im->count < MAX_BLOCKS) {
        im->size[im->count] = size;
        im->temporary[im->count] = temporary;
        im->checked[im->count] = checked;
        im->count++;
    }
}

/* @p app: the app itself (its 4 KB thread and records after loading) or a
 * module (the plugin manager's records). */
static bool read_image(const char* path, Image* im, bool app) {
    memset(im, 0, sizeof(*im));
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    static uint8_t d[4 << 20];
    size_t n = fread(d, 1, sizeof(d), f);
    bool ok = rg_elf_load_cost(file_read, f, &im->cost);
    fclose(f);
    if(!ok || n < 52) return false;
    uint32_t shoff = rd32(d + 32);
    uint16_t ent = (uint16_t)(d[46] | d[47] << 8), num = (uint16_t)(d[48] | d[49] << 8);
    uint16_t strndx = (uint16_t)(d[50] | d[51] << 8);
    uint32_t names = rd32(d + shoff + strndx * ent + 16);
    push(im, 200, false, false);
    push(im, app ? 120 : 80, false, false);
    push(im, 24, true, false);
    push(im, 560, true, false);
    if(!app) {
        push(im, 32, false, false);
        push(im, 16, false, false);
    }
    for(uint16_t i = 1; i < num; i++) {
        const uint8_t* sh = d + shoff + i * ent;
        const char* name = (const char*)d + names + rd32(sh);
        uint32_t flags = rd32(sh + 8), size = rd32(sh + 20), align = rd32(sh + 32);
        if(!align) align = 1;
        if(!strncmp(name, ".ARM.", 5) || !strncmp(name, ".rel.ARM.", 9) ||
           !strncmp(name, ".fast.rel.ARM.", 14))
            continue;
        if(flags & 2u) {
            push(im, (uint32_t)strlen(name) + 1, false, false);
            push(im, 24, false, false);
            if(size) push(im, size + align - 1 + 4, false, true);
        } else if(!strncmp(name, ".fast.rel", 9)) {
            push(im, 24, true, false);
            if(size) push(im, size + align - 1 + 4, true, true);
        } else if(!strcmp(name, ".gnu_debuglink")) {
            push(im, size, false, false);
        }
    }
    im->ok = true;
    return true;
}

/* Map @p im as the loader does; false (nothing kept) if a section check
 * or an allocation fails. Relocation frees the tables at the end. */
static bool map_image(const Image* im, Mapped* m) {
    memset(m, 0, sizeof(*m));
    for(size_t i = 0; i < im->count; i++) {
        if(im->checked[i] && heap4_max_free_block() < im->size[i] + SECTION_CHECK) goto fail;
        if(!(m->p[i] = heap4_malloc(im->size[i]))) goto fail;
    }
    for(size_t i = 0; i < im->count; i++)
        if(im->temporary[i]) {
            heap4_free(m->p[i]);
            m->p[i] = NULL;
        }
    m->loaded = true;
    return true;
fail:
    for(size_t i = im->count; i-- > 0;)
        heap4_free(m->p[i]);
    memset(m, 0, sizeof(*m));
    return false;
}

static void unmap_image(Mapped* m) {
    for(size_t i = MAX_BLOCKS; i-- > 0;)
        heap4_free(m->p[i]);
    memset(m, 0, sizeof(*m));
}

/* radiogeddon_module.c's check before loading a module with @p work. */
static size_t module_need(const Image* im, size_t work) {
    size_t later = im->cost.temporary > work ? im->cost.temporary : work;
    return im->cost.resident + MOD_LOADER + later + MOD_MARGIN;
}

static uint32_t rng = 1;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* A heap whose free space is one block of @p free_bytes (the rest held). */
static void* heap_with_free(size_t free_bytes) {
    heap4_init(ARENA);
    return heap4_malloc(ARENA - free_bytes - 2u * 8u);
}

/* ---- A and B: the Logger after the Adaptive Scanner ----------------------- */

/* Beta 12 device log (RogueMaster 4f2e06200, the owner's Flipper): the
 * scanner object and its mutex, the adaptive schedule (RgAdapt, 1,072 B on
 * the device), and the sweep thread (1,536 B stack + thread object and name):
 * "Adaptive Scanner started" took 4,360 B after the module, "Scanner
 * stopped" gave 1,888 B back. */
#define SCANNER_OBJECT 1480u
#define SCANNER_MUTEX  80u
#define SCANNER_ADAPT  1072u
#define SCANNER_STACK  1536u
#define SCANNER_THREAD 352u
#define LOG_WORK_SETUP (1u * 1024u) /* Adaptive setup: work_kb 1 */
#define LOG_WORK_RUN   (8u * 1024u) /* RF Activity Logger: work_kb 8 */

typedef struct {
    void* object;
    void* mutex;
    void* adapt;
    void* stack;
    void* thread;
} Scanner;

static bool scanner_make(Scanner* s) {
    if(!s->object) {
        s->object = heap4_malloc(SCANNER_OBJECT);
        s->mutex = heap4_malloc(SCANNER_MUTEX);
    }
    if(!s->adapt) s->adapt = heap4_malloc(SCANNER_ADAPT);
    return s->object && s->mutex && s->adapt;
}

static bool scanner_start(Scanner* s) {
    s->thread = heap4_malloc(SCANNER_THREAD);
    s->stack = heap4_malloc(SCANNER_STACK);
    return s->thread && s->stack;
}

static void scanner_stop(Scanner* s) {
    heap4_free(s->stack);
    heap4_free(s->thread);
    s->stack = s->thread = NULL;
}

static void scanner_free(Scanner* s) {
    scanner_stop(s);
    heap4_free(s->adapt);
    heap4_free(s->mutex);
    heap4_free(s->object);
    memset(s, 0, sizeof(*s));
}

static void step(const char* what) {
    printf(
        "    %-28s free %6zu, largest %6zu\n", what, heap4_free_bytes(), heap4_max_free_block());
}

/* One Adaptive Scanner start and stop, then the Logger: true when the
 * Logger's module passes radiogeddon_module.c's check. @p beta13 picks the
 * order; @p verbose prints every step. */
static bool adaptive_then_logger(const Image* log, bool beta13, bool verbose, Scanner* sc) {
    Mapped m;
    if(heap4_max_free_block() < module_need(log, LOG_WORK_SETUP) || !map_image(log, &m)) {
        if(verbose) step("setup refused");
        return false;
    }
    if(verbose) step("radiogeddon_log.fal loaded");
    // "Start scanner": beta 12 makes the scanner now, under the module;
    // beta 13 unloads the module first.
    if(beta13) {
        unmap_image(&m);
        if(verbose) step("module unloaded");
    }
    bool ok = scanner_make(sc) && scanner_start(sc);
    if(verbose) step(beta13 ? "Adaptive Scanner started" : "Adaptive Scanner started");
    if(!beta13) {
        unmap_image(&m);
        if(verbose) step("module unloaded");
    }
    scanner_stop(sc);
    if(verbose) step("Scanner stopped");
    if(!ok) return false;
    // The module again: its setup screen (work 1 KB; the device's "needs
    // 17088") and the Logger itself (work 8 KB).
    size_t need_setup = module_need(log, LOG_WORK_SETUP);
    size_t need = module_need(log, LOG_WORK_RUN);
    bool fits = heap4_max_free_block() >= need;
    if(verbose) {
        printf(
            "    setup again: needs %zu, %zu free -> %s\n",
            need_setup,
            heap4_max_free_block(),
            heap4_max_free_block() >= need_setup ? "loads" : "REFUSED");
        printf(
            "    Logger: needs %zu, %zu free -> %s\n",
            need,
            heap4_max_free_block(),
            fits ? "loads" : "REFUSED");
    }
    return heap4_max_free_block() >= need_setup;
}

static void flow_logger(const Image* log, const char* label) {
    printf("\nA. Adaptive Scanner, then the RF Activity Logger (%s's log module)\n", label);
    printf("   device log, beta 12: app start free 25568 / largest 25568; module loaded 15616 /\n"
           "   14864; scanner started 11256 / 11256; module unloaded 20744 / 11256; scanner\n"
           "   stopped 22632 / 13024; Logger needs 17088 -> refused.\n");
    for(int order = 0; order < 2; order++) {
        printf("   %s order, from one free block of 25568 B:\n", order ? "beta 13" : "beta 12");
        void* base = heap_with_free(25568u);
        Scanner sc = {0};
        adaptive_then_logger(log, order == 1, true, &sc);
        scanner_free(&sc);
        heap4_free(base);
    }

    printf("\nB. Adaptive Scanner -> Back -> Logger -> Back, 2,000 rounds per order, GUI blocks\n"
           "   in between, from free heaps of 22-40 KB in one or two blocks:\n");
    for(int order = 0; order < 2; order++) {
        unsigned refused = 0, rounds = 0, failures = 0;
        rng = 77;
        for(unsigned r = 0; r < 2000u; r++) {
            size_t free_bytes = 22u * 1024u + rnd() % (18u * 1024u);
            void* base = heap_with_free(free_bytes);
            void* gui[12] = {0};
            Scanner sc = {0};
            for(unsigned k = 0; k < 4; k++) {
                // A popup, a list, a string or two between screens.
                for(unsigned g = 0; g < 3; g++) {
                    size_t j = rnd() % 12;
                    if(gui[j]) {
                        heap4_free(gui[j]);
                        gui[j] = NULL;
                    } else {
                        gui[j] = heap4_malloc(16 + rnd() % 300);
                    }
                }
                rounds++;
                if(!adaptive_then_logger(log, order == 1, false, &sc)) refused++;
            }
            scanner_free(&sc);
            for(unsigned j = 0; j < 12; j++)
                heap4_free(gui[j]);
            Heap4Stats st;
            heap4_stats(&st);
            failures += st.failures;
            heap4_free(base);
        }
        printf(
            "   %s order: the log module refused after the scanner in %u of %u rounds; allocations"
            " that failed: %u\n",
            order ? "beta 13" : "beta 12",
            refused,
            rounds,
            failures);
    }
}

/* ---- C: a receive session -------------------------------------------------- */

/* RogueMaster 4f2e06200 (lib/subghz, furi/core), device sizes:
 *  - environment (24 B) and keystore: 117 keys from keeloq_mfcodes, each a
 *    24-byte array entry (the array doubling from 16) and a FuriString
 *    (16 B, plus its text when it does not fit inline: about half), with
 *    the loader's 2 line buffers (512 + 256 B) while reading;
 *  - the receiver (24 B) and its slot array (doubling from 16, 4 B each);
 *  - one decoder per protocol fed (60 here, about 100 B each; KeeLoq's and
 *    POCSAG's own strings), and in beta 12 also RAW's (48 B) and BinRAW's:
 *    its 180-byte object, 2,048 x 4 B and 2,048 B;
 *  - the worker: its object, a FuriThread with its name and two strings,
 *    a 2,048-byte stack and a 2,048 x 4 B stream buffer. */
#define KEYS     117u
#define DECODERS 60u

typedef struct {
    void* p[1024];
    size_t n;
} Session;

static bool take(Session* s, size_t size) {
    void* p = heap4_malloc(size);
    if(!p) return false;
    s->p[s->n++] = p;
    return true;
}

static void session_free(Session* s) {
    for(size_t i = s->n; i-- > 0;)
        heap4_free(s->p[i]);
    s->n = 0;
}

static bool worker_take(Session* s) {
    return take(s, 40) && take(s, 120) && take(s, 16) && take(s, 16) && take(s, 16) &&
           take(s, 2048) && take(s, 2048u * 4u + 40u);
}

/* Beta 13 makes the worker first (radiogeddon_subghz_decoders_alloc). */
static bool session_start(Session* s, bool beta13) {
    s->n = 0;
    if(beta13 && !worker_take(s)) return false;
    // Environment and keystore.
    if(!take(s, 24) || !take(s, 16)) return false;
    void* lines[2] = {heap4_malloc(512), heap4_malloc(256)};
    if(!lines[0] || !lines[1]) {
        heap4_free(lines[0]);
        heap4_free(lines[1]);
        return false;
    }
    void* array = NULL;
    size_t cap = 0;
    for(size_t k = 0; k < KEYS; k++) {
        if(k == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            void* grown = heap4_malloc(ncap * 24u);
            if(!grown) return false;
            heap4_free(array);
            array = grown;
            cap = ncap;
        }
        if(!take(s, 16)) return false;
        if((k & 1) && !take(s, 12)) return false;
    }
    heap4_free(lines[1]);
    heap4_free(lines[0]);
    s->p[s->n++] = array;
    // Receiver and decoders (beta 13: only those the filter feeds).
    if(!take(s, 24)) return false;
    void* slots = NULL;
    cap = 0;
    size_t count = beta13 ? DECODERS : DECODERS + 2u;
    for(size_t k = 0; k < count; k++) {
        if(k == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            void* grown = heap4_malloc(ncap * 4u);
            if(!grown) return false;
            heap4_free(slots);
            slots = grown;
            cap = ncap;
        }
        if(!take(s, 72 + (k * 37u) % 64u)) return false;
    }
    s->p[s->n++] = slots;
    if(!take(s, 16) || !take(s, 16) || !take(s, 16) || !take(s, 16)) return false; // strings
    if(!beta13) {
        if(!take(s, 48)) return false; // RAW
        if(!take(s, 180) || !take(s, 2048u * 4u) || !take(s, 2048u)) return false; // BinRAW
    }
    // Beta 12 made the worker last.
    if(!beta13 && !worker_take(s)) return false;
    return true;
}

static unsigned flow_crashes_b13;

static void flow_receiver(void) {
    printf(
        "\nC. A receive session, free heap in one region with 0-4 small blocks left in it\n   (2,000 heaps per row; RogueMaster sizes)\n");
    // The cost each would measure on a whole heap, as the app does.
    size_t cost[2];
    for(int b = 0; b < 2; b++) {
        void* base = heap_with_free(120u * 1024u);
        Heap4Stats before, after;
        heap4_stats(&before);
        heap4_reset_marks();
        Session s;
        session_start(&s, b == 1);
        heap4_stats(&after);
        cost[b] = before.free_bytes - after.min_free_bytes;
        session_free(&s);
        heap4_free(base);
    }
    printf(
        "   model cost: beta 12 %zu B, beta 13 %zu B (%zu B less; device beta 12 measured"
        " 36568 B)\n",
        cost[0],
        cost[1],
        cost[0] - cost[1]);
    printf(
        "   %-10s %10s %10s %10s %10s %10s %10s\n",
        "free",
        "b12 fits",
        "b12 start",
        "b12 crash",
        "b13 fits",
        "b13 start",
        "b13 crash");
    unsigned crash_total = 0, crash_b13_total = 0;
    static void* pins[4096];
    for(size_t kb = 16; kb <= 48; kb += 4) {
        unsigned fits[2] = {0}, starts[2] = {0}, crash[2] = {0};
        rng = 31u + (uint32_t)kb;
        for(unsigned h = 0; h < 2000u; h++) {
            size_t want = kb * 1024u + rnd() % 4096u;
            for(int b = 0; b < 2; b++) {
                uint32_t seed = rng;
                heap4_init(ARENA);
                // The same heap for both: the free space one region with K
                // small blocks left in it at random (as after other screens).
                void* base = heap4_malloc(ARENA - want - 64u);
                size_t n = 0;
                void* p;
                while(n < 4096 && (p = heap4_malloc(504)) != NULL)
                    pins[n++] = p;
                unsigned k = rnd() % 5u;
                for(unsigned i = 0; i < k && n; i++)
                    pins[rnd() % n] = NULL; // stays allocated
                for(size_t i = 0; i < n; i++)
                    heap4_free(pins[i]);
                n = 0;
                (void)base;
                // radiogeddon_scene_radio_memory_ok: the measured cost plus
                // the margin must be free.
                // Beta 13 also needs the worker's buffer (RG_MEM_SESSION_BLOCK,
                // 16,640 B) in one block.
                bool allowed = heap4_free_bytes() >= cost[b] + RADIO_MARGIN &&
                               (b == 0 || heap4_max_free_block() >= 16u * 1024u + 256u);
                Session s;
                bool ok = session_start(&s, b == 1);
                fits[b] += ok;
                starts[b] += allowed;
                crash[b] += allowed && !ok;
                session_free(&s);
                if(b == 0) rng = seed;
            }
        }
        printf(
            "   %3zu-%2zu KB %10u %10u %10u %10u %10u %10u\n",
            kb,
            kb + 4,
            fits[0],
            starts[0],
            crash[0],
            fits[1],
            starts[1],
            crash[1]);
        crash_total += crash[0] + crash[1];
        crash_b13_total += crash[1];
    }
    flow_crashes_b13 = crash_b13_total;
    printf(
        "   (fits: every allocation found a block; start: the preflight allows it, beta 12's"
        " rule or\n   beta 13's; crash: allowed but ran out)\n   sessions the preflight allowed that ran out: %u\n",
        crash_total);
}

/* ---- D: launch, use, close, again ------------------------------------------ */

/* What the app keeps from its start (scripts/struct_sizes.py; beta 12's
 * device log: 14,848 B between "launch" and "app start"), in blocks of the
 * sizes the objects have: the app (2,240 B), the GUI modules and views, the
 * view dispatcher and scene manager, the radio wrapper and the external
 * radio driver plugin (5,180 B of code with its records), the history, the
 * lab view. Beta 13 makes the scanner view (~0.9 KB) only on its screen. */
static const uint32_t k_start_b12[] = {2240, 5180, 96,  360, 1200, 64,  80,  520, 600, 420,
                                       400,  380,  320, 260, 240,  780, 120, 220, 200, 180,
                                       64,   96,   48,  32,  640,  120, 96,  64,  48,  32};
static const uint32_t k_start_b13[] = {2240, 5180, 96,  360, 1200, 64,  80,  520, 600, 420,
                                       400,  380,  320, 260, 240,  120, 220, 200, 180, 64,
                                       96,   48,   32,  640, 120,  96,  64,  48,  32};

typedef struct {
    Mapped image;
    void* keep[64];
    size_t nkeep;
} Run;

static bool app_launch(const Image* app, bool beta13, Run* r) {
    memset(r, 0, sizeof(*r));
    if(!map_image(app, &r->image)) return false;
    const uint32_t* sizes = beta13 ? k_start_b13 : k_start_b12;
    size_t n = beta13 ? sizeof(k_start_b13) / 4 : sizeof(k_start_b12) / 4;
    for(size_t i = 0; i < n; i++) {
        void* p = heap4_malloc(sizes[i]);
        if(!p) return false;
        r->keep[r->nkeep++] = p;
    }
    return true;
}

static void app_close(Run* r) {
    for(size_t i = r->nkeep; i-- > 0;)
        heap4_free(r->keep[i]);
    r->nkeep = 0;
    unmap_image(&r->image);
}

static void flow_relaunch(const Image* apps, const char* const* labels, size_t napps) {
    printf(
        "\nD. 60 launches in a row: use, close, the firmware keeps 0-2 small blocks each time;\n"
        "   every 3rd time another app ran in between (500 seeds, from 96-140 KB free)\n");
    for(size_t a = 0; a < napps; a++) {
        unsigned launched = 0, tried = 0, streak_total = 0;
        rng = 4242;
        for(unsigned seed = 0; seed < 500u; seed++) {
            heap4_init(ARENA);
            size_t free0 = 96u * 1024u + rnd() % (44u * 1024u);
            void* base = heap4_malloc(ARENA - free0);
            void* strays[64] = {0};
            size_t nstrays = 0;
            bool streak = true;
            for(unsigned i = 0; i < 60u; i++) {
                if(i % 3 == 2) {
                    // Another app: blocks of all sizes, most freed again.
                    void* other[256];
                    size_t n = 0;
                    void* p;
                    while(n < 256 && heap4_free_bytes() > 30u * 1024u &&
                          (p = heap4_malloc(32 + rnd() % 3000)) != NULL)
                        other[n++] = p;
                    for(size_t k = 0; k < n; k++) {
                        if(rnd() % 50 == 0 && nstrays < 64) {
                            strays[nstrays++] = other[k]; // its service keeps it
                        } else {
                            heap4_free(other[k]);
                        }
                    }
                }
                Run r;
                tried++;
                bool ok = app_launch(&apps[a], strstr(labels[a], "13") != NULL, &r);
                launched += ok;
                if(!ok) streak = false;
                if(streak) streak_total++;
                // Using it: screens and strings come and go.
                void* use[16];
                for(unsigned k = 0; k < 16; k++)
                    use[k] = ok ? heap4_malloc(16 + rnd() % 600) : NULL;
                for(unsigned k = 0; k < 16; k++)
                    heap4_free(use[k]);
                app_close(&r);
                // Services (desktop animation, storage, timers) allocate.
                for(unsigned k = rnd() % 3; k > 0 && nstrays < 64; k--)
                    strays[nstrays++] = heap4_malloc(64 + rnd() % 900);
                // ...and free some of what they held earlier.
                if(nstrays && rnd() % 2) {
                    size_t j = rnd() % nstrays;
                    heap4_free(strays[j]);
                    strays[j] = strays[--nstrays];
                }
            }
            for(size_t k = 0; k < nstrays; k++)
                heap4_free(strays[k]);
            heap4_free(base);
        }
        printf(
            "   %-8s launched %5u of %5u; launches before the first failure, on average %.1f of 60\n",
            labels[a],
            launched,
            tried,
            streak_total / 500.0);
    }
}

int main(int argc, char** argv) {
    // argv: NEW_APP NEW_FAL_DIR [OLD_APP OLD_FAL_DIR]
    if(argc != 3 && argc != 5) {
        fprintf(stderr, "usage: %s NEW.fap NEW_FAL_DIR [OLD.fap OLD_FAL_DIR]\n", argv[0]);
        return 2;
    }
    char path[1024];
    static Image log_new, log_old, apps[2];
    snprintf(path, sizeof(path), "%s/radiogeddon_log.fal", argv[2]);
    if(!read_image(path, &log_new, false) || !read_image(argv[1], &apps[0], true)) {
        fprintf(stderr, "cannot read %s or %s\n", path, argv[1]);
        return 2;
    }
    const char* labels[2] = {"beta 13", "beta 12"};
    size_t napps = 1;
    if(argc == 5) {
        snprintf(path, sizeof(path), "%s/radiogeddon_log.fal", argv[4]);
        if(!read_image(path, &log_old, false) || !read_image(argv[3], &apps[1], true)) {
            fprintf(stderr, "cannot read %s or %s\n", path, argv[3]);
            return 2;
        }
        napps = 2;
        flow_logger(&log_old, "beta 12");
    }
    flow_logger(&log_new, "beta 13");
    flow_receiver();
    flow_relaunch(apps, labels, napps);
    // Beta 13's rules must never let a session start that runs out.
    if(flow_crashes_b13) {
        printf("FAILED: beta 13's preflight allowed %u sessions that ran out\n", flow_crashes_b13);
        return 1;
    }
    return 0;
}
