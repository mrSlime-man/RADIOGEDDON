/*
 * Launching the app on a fragmented heap, simulated: how often does the
 * firmware's loader find room for the .fap's sections?
 *
 *   make -C test benchlaunch FAPS="b12=<beta 12 .fap> b13=<beta 13 .fap> ..."
 *   (or FAP_A=<old .fap> FAP_B=<new .fap>, as before)
 *
 * On a device that has run other apps (or this one, just closed), the free
 * heap is in pieces. The loader (lib/flipper_application/elf/elf_file.c,
 * the same in the five pinned trees) maps every allocatable section into a
 * heap block of its own and refuses the launch when the largest free block
 * is smaller than the section plus 1 KB (elf_load_section_data). Beta 11's
 * code was one 70,940-byte .text section, beta 12's two (.text, .rgtext),
 * beta 13's five smaller ones (radiogeddon_edition.h).
 *
 * For each of many heaps of the model of the firmware's allocator
 * (stubs/heap4.h), this replays the loader's allocations for each .fap in
 * the firmware's order (flipper_application_preload, _map_to_memory, then
 * _alloc_thread): its fixed records; per allocatable section the name, the
 * dictionary node and the aligned block behind the 1 KB check; per section
 * the fast relocation table behind the same check; then relocation, which
 * frees those tables; then the app thread's 4 KB stack and records. A heap
 * counts as a launch when all of it fits. Every file sees exactly the same
 * heaps. A simulation, not a measurement on a Flipper.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stubs/heap4.h"

#define ARENA         (180u * 1024u)
#define HEAPS         2000u
#define MAX_BLOCKS    96u
#define MAX_FILES     8u
#define SECTION_CHECK 1024u /* elf_load_section_data: size + 1024 must fit */
#define APP_STACK     (4u * 1024u) /* application.fam stack_size */

typedef struct {
    const char* label;
    uint32_t size[MAX_BLOCKS];
    uint8_t temporary[MAX_BLOCKS];
    uint8_t checked[MAX_BLOCKS]; /* a section: the loader checks size + 1 KB first */
    uint8_t after_load[MAX_BLOCKS]; /* after relocation freed the tables */
    size_t count;
    uint32_t largest; /* largest allocatable section */
    uint32_t largest_table; /* largest relocation table */
    uint32_t sections; /* allocatable bytes */
    uint32_t tables; /* relocation table bytes */
    unsigned alloc_sections;
} Load;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void push(Load* l, uint32_t size, bool temporary, bool checked, bool after_load) {
    if(l->count < MAX_BLOCKS) {
        l->size[l->count] = size;
        l->temporary[l->count] = temporary;
        l->checked[l->count] = checked;
        l->after_load[l->count] = after_load;
        l->count++;
    }
}

static bool read_load(const char* path, Load* l) {
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
    push(l, 200, false, false, false);
    push(l, 120, false, false, false);
    push(l, 24, true, false, false);
    push(l, 560, true, false, false);
    for(uint16_t i = 1; i < num; i++) {
        const uint8_t* sh = d + shoff + i * ent;
        const char* name = (const char*)d + names + rd32(sh);
        uint32_t flags = rd32(sh + 8), size = rd32(sh + 20), align = rd32(sh + 32);
        if(!align) align = 1;
        if(!strncmp(name, ".ARM.", 5) || !strncmp(name, ".rel.ARM.", 9) ||
           !strncmp(name, ".fast.rel.ARM.", 14))
            continue;
        if(flags & 2u) {
            push(l, (uint32_t)strlen(name) + 1, false, false, false);
            push(l, 24, false, false, false);
            if(size) push(l, size + align - 1 + 4, false, true, false);
            l->sections += size;
            l->alloc_sections++;
            if(size > l->largest) l->largest = size;
        } else if(!strncmp(name, ".fast.rel", 9)) {
            push(l, 24, true, false, false);
            if(size) push(l, size + align - 1 + 4, true, true, false);
            l->tables += size;
            if(size > l->largest_table) l->largest_table = size;
        }
    }
    // The app's thread: its stack and the thread and task records.
    push(l, APP_STACK, false, false, true);
    push(l, 400, false, false, true);
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
    bool relocated = false;
    for(size_t i = 0; i < l->count && ok; i++) {
        if(l->after_load[i] && !relocated) {
            // flipper_application_map_to_memory: relocation frees the tables.
            for(size_t k = 0; k < i; k++) {
                if(l->temporary[k]) {
                    heap4_free(p[k]);
                    p[k] = NULL;
                }
            }
            relocated = true;
        }
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

static Load loads[MAX_FILES];
static size_t nloads;

/* Runs every file on the current heap; counts into @p fits, and into
 * @p only_base the heaps where the first file fits and another does not. */
static void run_all(unsigned* fits, unsigned* only_base) {
    bool base = false;
    for(size_t f = 0; f < nloads; f++) {
        bool ok = replay(&loads[f]);
        if(f == 0) base = ok;
        fits[f] += ok;
        if(f > 0 && base && !ok) only_base[f]++;
    }
}

int main(int argc, char** argv) {
    if(argc < 3 || argc - 1 > (int)MAX_FILES) {
        fprintf(stderr, "usage: %s [LABEL=]FILE.fap [LABEL=]FILE.fap ... (2 to 8)\n", argv[0]);
        return 2;
    }
    for(int a = 1; a < argc; a++) {
        Load* l = &loads[nloads];
        memset(l, 0, sizeof(*l));
        char* eq = strchr(argv[a], '=');
        const char* path = argv[a];
        if(eq) {
            *eq = '\0';
            l->label = argv[a];
            path = eq + 1;
        } else {
            l->label = nloads == 0 ? "old" : nloads == 1 ? "new" : argv[a];
        }
        if(!read_load(path, l)) {
            fprintf(stderr, "cannot read %s\n", path);
            return 2;
        }
        nloads++;
    }
    printf(
        "%-10s %9s %9s %9s %9s %9s\n",
        "file",
        "sections",
        "largest",
        "count",
        "rel.tabs",
        "largest");
    for(size_t f = 0; f < nloads; f++)
        printf(
            "%-10s %9u %9u %9u %9u %9u\n",
            loads[f].label,
            loads[f].sections,
            loads[f].largest,
            loads[f].alloc_sections,
            loads[f].tables,
            loads[f].largest_table);

    static void* pins[8192];
    unsigned fits[MAX_FILES] = {0}, only[MAX_FILES] = {0}, tot = 0;
    printf(
        "\nrandomly fragmented heaps (%u per row), launches that fit:\n%-12s", HEAPS, "free heap");
    for(size_t f = 0; f < nloads; f++)
        printf(" %9s", loads[f].label);
    printf("\n");
    for(size_t kb = 72; kb <= 160; kb += 8) {
        unsigned row[MAX_FILES] = {0}, row_only[MAX_FILES] = {0};
        rng = 11u + (uint32_t)kb;
        for(unsigned h = 0; h < HEAPS; h++) {
            heap4_init(ARENA);
            size_t want = kb * 1024u + rnd() % 8192u;
            size_t np = fragment(pins, sizeof(pins) / sizeof(pins[0]), want);
            run_all(row, row_only);
            for(size_t i = 0; i < np; i++)
                heap4_free(pins[i]);
        }
        printf("%3zu-%3zu KB  ", kb, kb + 8);
        for(size_t f = 0; f < nloads; f++) {
            printf(" %9u", row[f]);
            fits[f] += row[f];
            only[f] += row_only[f];
        }
        printf("\n");
        tot += HEAPS;
    }
    printf("total of %u ", tot);
    for(size_t f = 0; f < nloads; f++)
        printf(" %s %u", loads[f].label, fits[f]);
    printf("\nheaps where %s fits and the other does not:", loads[0].label);
    for(size_t f = 1; f < nloads; f++)
        printf(" %s %u", loads[f].label, only[f]);
    printf("\n");

    // Right after an app closed: the firmware's own blocks below, the rest
    // free except a few small blocks services allocated meanwhile (an
    // animation frame, a file object, a timer...) at random places in it.
    printf("\nafter an app closed: free heap with K small blocks left in it, %% that fit\n");
    printf("%-8s", "free");
    for(unsigned k = 0; k <= 6; k++)
        printf("  K=%u%*s", k, (int)(7 * nloads - 3), "");
    printf("\n");
    unsigned fits2[MAX_FILES] = {0}, only2[MAX_FILES] = {0}, tot2 = 0;
    for(size_t kb = 72; kb <= 136; kb += 8) {
        printf("%3zu KB  ", kb);
        for(unsigned k = 0; k <= 6; k++) {
            unsigned row[MAX_FILES] = {0};
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
                run_all(row, only2);
                (void)base;
            }
            printf(" ");
            for(size_t f = 0; f < nloads; f++) {
                printf("%5.1f%s", 100.0 * row[f] / HEAPS, f + 1 < nloads ? "/" : " ");
                fits2[f] += row[f];
            }
            tot2 += HEAPS;
        }
        printf("\n");
    }
    printf("total of %u ", tot2);
    for(size_t f = 0; f < nloads; f++)
        printf(" %s %u", loads[f].label, fits2[f]);
    printf("\nheaps where %s fits and the other does not:", loads[0].label);
    for(size_t f = 1; f < nloads; f++)
        printf(" %s %u", loads[f].label, only2[f]);
    printf("\n");
    return 0;
}
