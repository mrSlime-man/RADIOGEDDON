/*
 * Module loading on a fragmented heap, simulated: is the free-block check
 * the app makes before loading a module (radiogeddon_module.c) safe, and how
 * often does it refuse a load that would have fitted?
 *
 *   make -C test benchmodules FAL_DIR=<folder of built .fal files>
 *
 * For each built module and each of many heaps, fragmented at random by
 * the heap model of the firmware's allocator (stubs/heap4.h), this replays
 * what the firmware 1.4.3 loader allocates for that file (its fixed
 * records, every loaded section in an aligned block of its own with its
 * bookkeeping, the debug link, the relocation tables, which are freed after
 * relocation, and the file object, freed at the end), then the module's
 * work as one block, and asks whether every allocation found a block with
 * MARGIN bytes still free in one piece afterwards. It compares that with
 * the two rules:
 *
 *   beta 10: sections + 6 KB + work + 4 KB <= largest free block
 *   beta 11: radiogeddon_module_need() (rg_elf.h's load cost, 2 KB of loader
 *            records, the larger of the relocation tables and the work,
 *            4 KB) <= largest free block
 *
 * "unsafe" counts heaps where a rule allowed the load and the replay ran out
 * of memory (on the device: a crash); "refused" counts heaps where a rule
 * refused a load the replay completed. Device sizes throughout; a
 * simulation, not a measurement on a Flipper.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stubs/heap4.h"
#include "../helpers/rg_elf.h"

#define MARGIN     (4u * 1024u)
#define LOADER_B10 (6u * 1024u)
#define LOADER_B11 (2u * 1024u)
#define HEAPS      4000u
#define ARENA      (180u * 1024u)
#define MAX_BLOCKS 64u

/* Work besides the module, as scenes/radiogeddon_scene_tools.c asks. */
static const struct {
    const char* name;
    unsigned kb;
} k_work[] = {
    {"radiogeddon_fp.fal", 16},
    {"radiogeddon_studio.fal", 20},
    {"radiogeddon_reports.fal", 14},
    {"radiogeddon_log.fal", 8},
    {"radiogeddon_heatmap.fal", 12},
    {"radiogeddon_about.fal", 2},
    {"radiogeddon_lab.fal", 4},
    {"radiogeddon_range.fal", 4},
    {"radiogeddon_settings.fal", 1},
    {"radiogeddon_hopper.fal", 1},
    {"radiogeddon_bits.fal", 15},
};

typedef struct {
    uint32_t size[MAX_BLOCKS]; /* allocations in the loader's order */
    uint8_t temporary[MAX_BLOCKS]; /* freed once relocated */
    size_t count;
    size_t sections;
    RgElfCost cost;
} Load;

static bool file_read(void* context, uint32_t offset, void* buf, size_t len) {
    FILE* f = context;
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void push(Load* l, uint32_t size, bool temporary) {
    if(l->count < MAX_BLOCKS) {
        l->size[l->count] = size;
        l->temporary[l->count] = temporary;
        l->count++;
    }
}

/* The firmware loader's allocations for one .fal, in its order. */
static bool read_load(const char* path, Load* l) {
    memset(l, 0, sizeof(*l));
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    static uint8_t d[1 << 20];
    size_t n = fread(d, 1, sizeof(d), f);
    bool ok = rg_elf_load_cost(file_read, f, &l->cost);
    fclose(f);
    if(!ok || n < 52) return false;
    uint32_t shoff = rd32(d + 32);
    uint16_t ent = (uint16_t)(d[46] | d[47] << 8), num = (uint16_t)(d[48] | d[49] << 8);
    uint16_t strndx = (uint16_t)(d[50] | d[51] << 8);
    uint32_t names = rd32(d + shoff + strndx * ent + 16);
    // Fixed records before the sections: the application and its manifest,
    // the ELF file record, the file handle and the storage service's file
    // object (with its 512-byte sector buffer), the plugin manager and list.
    push(l, 200, false);
    push(l, 80, false);
    push(l, 24, true);
    push(l, 560, true);
    push(l, 32, false);
    push(l, 16, false);
    for(uint16_t i = 1; i < num; i++) {
        const uint8_t* sh = d + shoff + i * ent;
        const char* name = (const char*)d + names + rd32(sh);
        uint32_t flags = rd32(sh + 8), size = rd32(sh + 20), align = rd32(sh + 32);
        if(!align) align = 1;
        if(!strncmp(name, ".ARM.", 5) || !strncmp(name, ".rel.ARM.", 9) ||
           !strncmp(name, ".fast.rel.ARM.", 14))
            continue;
        if(flags & 2u) {
            push(l, (uint32_t)strlen(name) + 1, false); // strdup'd name
            push(l, 24, false); // dictionary node
            if(size) push(l, size + align - 1 + 4, false);
            l->sections += size;
        } else if(!strncmp(name, ".fast.rel", 9)) {
            push(l, 24, true);
            if(size) push(l, size + align - 1 + 4, true);
        } else if(!strcmp(name, ".gnu_debuglink")) {
            push(l, size, false);
        }
    }
    return true;
}

static unsigned work_of(const char* name) {
    for(size_t i = 0; i < sizeof(k_work) / sizeof(k_work[0]); i++)
        if(!strcmp(k_work[i].name, name)) return k_work[i].kb * 1024u;
    return 0;
}

static uint32_t rng = 1;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* A heap after a long session: blocks of mixed sizes allocated, a random
 * half freed, then a random part of the rest freed, until a target amount
 * is free. Returns the pins to release. */
static size_t fragment(void** pins, size_t max) {
    size_t n = 0;
    size_t want_free = 20u * 1024u + rnd() % (100u * 1024u);
    while(n < max) {
        uint32_t r = rnd() % 100u;
        size_t sz = r < 60 ? 16 + rnd() % 200 : r < 90 ? 200 + rnd() % 2000 : 2000 + rnd() % 9000;
        void* p = heap4_malloc(sz);
        if(!p) break;
        pins[n++] = p;
    }
    for(size_t i = 0; i < 4u * n && heap4_free_bytes() < want_free; i++) {
        size_t j = rnd() % n;
        if(pins[j]) {
            heap4_free(pins[j]);
            pins[j] = NULL;
        }
    }
    return n;
}

/* Replay the load and the work; true if every allocation fit and MARGIN
 * stays free in one block. Frees everything it allocated. */
static bool replay(const Load* l, unsigned work) {
    void* p[MAX_BLOCKS] = {0};
    bool ok = true;
    for(size_t i = 0; i < l->count && ok; i++)
        ok = (p[i] = heap4_malloc(l->size[i])) != NULL;
    for(size_t i = 0; i < l->count; i++)
        if(l->temporary[i] && p[i]) {
            heap4_free(p[i]);
            p[i] = NULL;
        }
    void* w = ok && work ? heap4_malloc(work) : NULL;
    if(work && !w) ok = false;
    if(ok && heap4_max_free_block() < MARGIN) ok = false;
    heap4_free(w);
    for(size_t i = l->count; i-- > 0;)
        heap4_free(p[i]);
    return ok;
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "usage: %s FAL_DIR\n", argv[0]);
        return 2;
    }
    DIR* dir = opendir(argv[1]);
    if(!dir) return 2;
    char names[32][64];
    size_t mods = 0;
    struct dirent* e;
    while((e = readdir(dir)) && mods < 32)
        if(strstr(e->d_name, ".fal") && strlen(e->d_name) < 64) strcpy(names[mods++], e->d_name);
    closedir(dir);
    for(size_t i = 1; i < mods; i++)
        for(size_t j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[64];
            memcpy(t, names[j], 64);
            memcpy(names[j], names[j - 1], 64);
            memcpy(names[j - 1], t, 64);
        }
    printf(
        "%-26s %8s %8s | %7s %7s | %7s %7s %7s | %7s %7s %7s\n",
        "module",
        "need b10",
        "need b11",
        "fits",
        "of",
        "b10 ok",
        "unsafe",
        "refused",
        "b11 ok",
        "unsafe",
        "refused");
    unsigned unsafe11 = 0, unsafe10 = 0, refused10 = 0, refused11 = 0, fits_total = 0;
    static void* pins[4096];
    for(size_t m = 0; m < mods; m++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", argv[1], names[m]);
        Load l;
        if(!read_load(path, &l)) {
            printf("%s: not readable\n", names[m]);
            return 1;
        }
        unsigned work = work_of(names[m]);
        size_t b10 = l.sections + LOADER_B10 + work + MARGIN;
        size_t later = l.cost.temporary > work ? l.cost.temporary : work;
        size_t b11 = l.cost.resident + LOADER_B11 + later + MARGIN;
        unsigned fits = 0, ok10 = 0, ok11 = 0, bad10 = 0, bad11 = 0, ref10 = 0, ref11 = 0;
        rng = 7u + (uint32_t)m;
        for(unsigned h = 0; h < HEAPS; h++) {
            heap4_init(ARENA);
            size_t np = fragment(pins, sizeof(pins) / sizeof(pins[0]));
            size_t maxfree = heap4_max_free_block();
            bool fit = replay(&l, work);
            bool a10 = maxfree >= b10, a11 = maxfree >= b11;
            fits += fit;
            ok10 += a10;
            ok11 += a11;
            bad10 += a10 && !fit;
            bad11 += a11 && !fit;
            ref10 += !a10 && fit;
            ref11 += !a11 && fit;
            for(size_t i = 0; i < np; i++)
                heap4_free(pins[i]);
        }
        printf(
            "%-26s %8zu %8zu | %7u %7u | %7u %7u %7u | %7u %7u %7u\n",
            names[m],
            b10,
            b11,
            fits,
            HEAPS,
            ok10,
            bad10,
            ref10,
            ok11,
            bad11,
            ref11);
        unsafe10 += bad10;
        unsafe11 += bad11;
        refused10 += ref10;
        refused11 += ref11;
        fits_total += fits;
    }
    printf(
        "\nloads that fit: %u; refused although they fit: beta 10 %u, beta 11 %u;"
        " allowed but ran out: beta 10 %u, beta 11 %u\n",
        fits_total,
        refused10,
        refused11,
        unsafe10,
        unsafe11);
    // Repeated load and unload of every module, as a long session does,
    // with the rest of the app's blocks coming and going in between: each
    // load is checked with beta 11's rule first, nothing may fail or leak,
    // and the free space must join up again.
    heap4_init(ARENA);
    static Load loads[32];
    for(size_t m = 0; m < mods; m++) {
        char path[4200];
        snprintf(path, sizeof(path), "%s/%s", argv[1], names[m]);
        read_load(path, &loads[m]);
    }
    size_t start_block = heap4_max_free_block();
    void* rest[48] = {0};
    unsigned cycles = 0, refused = 0;
    rng = 99;
    for(unsigned it = 0; it < 20000u; it++) {
        size_t k = rnd() % 48;
        if(rest[k]) {
            heap4_free(rest[k]);
            rest[k] = NULL;
        } else {
            rest[k] = heap4_malloc(16 + rnd() % 1500);
        }
        size_t m = rnd() % mods;
        unsigned work = work_of(names[m]);
        size_t later = loads[m].cost.temporary > work ? loads[m].cost.temporary : work;
        size_t need = loads[m].cost.resident + LOADER_B11 + later + MARGIN;
        if(heap4_max_free_block() < need) {
            refused++;
            continue;
        }
        if(!replay(&loads[m], work)) unsafe11++;
        cycles++;
    }
    for(size_t k = 0; k < 48; k++)
        heap4_free(rest[k]);
    Heap4Stats st;
    heap4_stats(&st);
    bool joined = st.max_free_block == start_block && st.used_bytes == 0 && heap4_check();
    printf(
        "repeated loads: %u loaded and unloaded, %u refused, %u ran out, %u failed allocations, "
        "heap %s afterwards\n",
        cycles,
        refused,
        unsafe11,
        (unsigned)st.failures,
        joined ? "whole again" : "NOT restored");
    return unsafe11 || !joined || st.failures ? 1 : 0;
}
