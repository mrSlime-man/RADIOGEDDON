/*
 * Launching the app on a fragmented heap, simulated: how often does the
 * firmware's loader find room for the .fap's sections?
 *
 *   make -C test benchlaunch FAP_A=<old .fap> FAP_B=<new .fap>
 *
 * On a device that has run other apps (or this one, just closed), the free
 * heap is in pieces. The loader (lib/flipper_application/elf/elf_file.c)
 * maps every allocatable section into a heap block of its own and refuses
 * the launch when the largest free block is smaller than the section plus
 * 1 KB (elf_load_section_data). Beta 11's code was one 70,940-byte .text
 * section; beta 12 puts the screens' code in a second section (.rgtext,
 * RG_UI_TEXT), so the same code needs two blocks of about half the size.
 *
 * For each of many heaps fragmented at random by the model of the
 * firmware's allocator (stubs/heap4.h), grouped by how much is free, this
 * replays the loader's allocations for each .fap in its order: its fixed
 * records, per allocatable section the name, the dictionary node and the
 * aligned block behind the 1 KB check, the relocation tables (freed after
 * relocation), then the app thread's 4 KB stack and records. A heap counts
 * as a launch when all of it fits. Both files see exactly the same heaps.
 * A simulation, not a measurement on a Flipper.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stubs/heap4.h"

#define ARENA         (180u * 1024u)
#define HEAPS         2000u
#define MAX_BLOCKS    64u
#define SECTION_CHECK 1024u /* elf_load_section_data: size + 1024 must fit */
#define APP_STACK     (4u * 1024u) /* application.fam stack_size */

typedef struct {
    uint32_t size[MAX_BLOCKS];
    uint8_t temporary[MAX_BLOCKS];
    uint8_t checked[MAX_BLOCKS]; /* a section: the loader checks size + 1 KB first */
    size_t count;
    uint32_t largest; /* largest allocatable section */
    uint32_t sections; /* allocatable bytes */
    unsigned code_blocks;
} Load;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void push(Load* l, uint32_t size, bool temporary, bool checked) {
    if(l->count < MAX_BLOCKS) {
        l->size[l->count] = size;
        l->temporary[l->count] = temporary;
        l->checked[l->count] = checked;
        l->count++;
    }
}

static bool read_load(const char* path, Load* l) {
    memset(l, 0, sizeof(*l));
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    static uint8_t d[4 << 20];
    size_t n = fread(d, 1, sizeof(d), f);
    fclose(f);
    if(n < 52 || memcmp(
                     d,
                     "\x7f"
                     "ELF",
                     4))
        return false;
    uint32_t shoff = rd32(d + 32);
    uint16_t ent = (uint16_t)(d[46] | d[47] << 8), num = (uint16_t)(d[48] | d[49] << 8);
    uint16_t strndx = (uint16_t)(d[50] | d[51] << 8);
    if(shoff + (size_t)num * ent > n) return false;
    uint32_t names = rd32(d + shoff + strndx * ent + 16);
    // The application, its ELF record, manifest and section dictionary, the
    // open file and the storage service's file object (sector buffer).
    push(l, 200, false, false);
    push(l, 120, false, false);
    push(l, 24, true, false);
    push(l, 560, true, false);
    for(uint16_t i = 1; i < num; i++) {
        const uint8_t* sh = d + shoff + i * ent;
        const char* name = (const char*)d + names + rd32(sh);
        uint32_t flags = rd32(sh + 8), size = rd32(sh + 20), align = rd32(sh + 32);
        if(!align) align = 1;
        if(!strncmp(name, ".ARM.", 5) || !strncmp(name, ".rel.ARM.", 9) ||
           !strncmp(name, ".fast.rel.ARM.", 14))
            continue;
        if(flags & 2u) {
            push(l, (uint32_t)strlen(name) + 1, false, false);
            push(l, 24, false, false);
            if(size) push(l, size + align - 1 + 4, false, true);
            l->sections += size;
            if(size > l->largest) l->largest = size;
            if(flags & 4u) l->code_blocks++;
        } else if(!strncmp(name, ".fast.rel", 9)) {
            push(l, 24, true, false);
            if(size) push(l, size + align - 1 + 4, true, true);
        }
    }
    // The app's thread: its stack and the thread and task records.
    push(l, APP_STACK, false, false);
    push(l, 400, false, false);
    return true;
}

static uint32_t rng = 1;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* A heap after a session: mixed blocks allocated, freed at random until
 * @p want_free bytes are free. Returns the pins to release. */
static size_t fragment(void** pins, size_t max, size_t want_free) {
    size_t n = 0;
    while(n < max) {
        uint32_t r = rnd() % 100u;
        size_t sz = r < 60 ? 16 + rnd() % 200 : r < 90 ? 200 + rnd() % 2000 : 2000 + rnd() % 9000;
        void* p = heap4_malloc(sz);
        if(!p) break;
        pins[n++] = p;
    }
    for(size_t i = 0; i < 8u * n && heap4_free_bytes() < want_free; i++) {
        size_t j = rnd() % n;
        if(pins[j]) {
            heap4_free(pins[j]);
            pins[j] = NULL;
        }
    }
    return n;
}

static bool replay(const Load* l) {
    void* p[MAX_BLOCKS] = {0};
    bool ok = true;
    for(size_t i = 0; i < l->count && ok; i++) {
        if(l->checked[i] && heap4_max_free_block() < l->size[i] + SECTION_CHECK) {
            ok = false;
            break;
        }
        ok = (p[i] = heap4_malloc(l->size[i])) != NULL;
    }
    for(size_t i = l->count; i-- > 0;)
        heap4_free(p[i]);
    return ok;
}

int main(int argc, char** argv) {
    if(argc < 3) {
        fprintf(stderr, "usage: %s OLD.fap NEW.fap\n", argv[0]);
        return 2;
    }
    Load a, b;
    if(!read_load(argv[1], &a) || !read_load(argv[2], &b)) {
        fprintf(stderr, "cannot read the .fap files\n");
        return 2;
    }
    printf(
        "old: %u B in sections, largest %u B, %u code block(s)\n"
        "new: %u B in sections, largest %u B, %u code block(s)\n\n",
        a.sections,
        a.largest,
        a.code_blocks,
        b.sections,
        b.largest,
        b.code_blocks);
    printf("%-14s %8s %8s %8s\n", "free heap", "heaps", "old fits", "new fits");
    static void* pins[8192];
    unsigned tot_a = 0, tot_b = 0, tot = 0, worse = 0;
    for(size_t kb = 96; kb <= 160; kb += 8) {
        unsigned fa = 0, fb = 0;
        rng = 11u + (uint32_t)kb;
        for(unsigned h = 0; h < HEAPS; h++) {
            heap4_init(ARENA);
            size_t want = kb * 1024u + rnd() % 8192u;
            size_t np = fragment(pins, sizeof(pins) / sizeof(pins[0]), want);
            bool ra = replay(&a);
            bool rb = replay(&b);
            fa += ra;
            fb += rb;
            worse += ra && !rb;
            for(size_t i = 0; i < np; i++)
                heap4_free(pins[i]);
        }
        printf("%3zu-%3zu KB    %8u %8u %8u\n", kb, kb + 8, HEAPS, fa, fb);
        tot_a += fa;
        tot_b += fb;
        tot += HEAPS;
    }
    printf(
        "\nlaunches that fit: old %u of %u, new %u of %u; heaps where only the old fit: %u\n",
        tot_a,
        tot,
        tot_b,
        tot,
        worse);

    // Right after an app closed: the firmware's own blocks below, the rest
    // free except a few small blocks services allocated meanwhile (an
    // animation frame, a file object, a timer...) at random places in it.
    printf("\nafter an app closed: free heap with K small blocks left in it\n%-10s", "free \\ K");
    for(unsigned k = 0; k <= 6; k++)
        printf("   K=%u old/new", k);
    printf("\n");
    unsigned tot2_a = 0, tot2_b = 0, worse2 = 0;
    for(size_t kb = 88; kb <= 136; kb += 8) {
        printf("%3zu KB    ", kb);
        for(unsigned k = 0; k <= 6; k++) {
            unsigned fa = 0, fb = 0;
            rng = 1000u + (uint32_t)kb * 7u + k;
            for(unsigned h = 0; h < HEAPS; h++) {
                heap4_init(ARENA);
                // Firmware blocks: everything but the free region.
                void* base = heap4_malloc(ARENA - kb * 1024u - 64u);
                // The free region in 512-byte blocks, then all freed but K.
                size_t n = 0;
                void* p;
                while(n < sizeof(pins) / sizeof(pins[0]) && (p = heap4_malloc(512 - 8)))
                    pins[n++] = p;
                for(unsigned i = 0; i < k && n; i++) {
                    size_t j = rnd() % n;
                    if(pins[j] == NULL) continue;
                    pins[j] = NULL; // stays allocated
                }
                for(size_t i = 0; i < n; i++)
                    heap4_free(pins[i]);
                bool ra = replay(&a), rb = replay(&b);
                fa += ra;
                fb += rb;
                worse2 += ra && !rb;
                (void)base;
            }
            tot2_a += fa;
            tot2_b += fb;
            printf(" %5.1f%%/%5.1f%%", 100.0 * fa / HEAPS, 100.0 * fb / HEAPS);
        }
        printf("\n");
    }
    printf(
        "launches that fit: old %u, new %u (of %u); heaps where only the old fit: %u\n",
        tot2_a,
        tot2_b,
        7u * 7u * HEAPS,
        worse2);
    return 0;
}
