#define _POSIX_C_SOURCE 200809L
/* Host benchmark of the Database engine (helpers/rg_db.c): what the Database
 * screen does with an index of N recordings, timed on the host CPU.
 *
 *   make -C test bench
 *
 * Per N it times one load's duplicate work (rg_db_needs_content_hash for every
 * entry, then rg_db_mark_duplicates) and one rg_db_select per sort order and
 * for a name search, as the list does on every sort, filter or search change.
 * It also prints a checksum of every result (the duplicate flags and counts,
 * each selection's order), which must not change when the engine is
 * optimized: the same index gives the same list. The recordings are
 * synthetic (names, protocols, sizes, times from a fixed seed).
 *
 * Host times only show relative cost (the Flipper's Cortex-M4 at 64 MHz is
 * far slower); they are not device measurements. Uses only the public API,
 * so the same file builds against an older rg_db.c for a before/after. */
#include "helpers/rg_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static const char* protos[] = {"Princeton", "CAME", "Nice FLO", "Holtek", "Linear", "GateTX",
                               "Chamberlain", "Ansonic", "BETT", "Doitrand", "Marantec", "KeeLoq"};

static uint32_t fold(uint32_t h, uint32_t v) {
    return (h ^ v) * 16777619u;
}

/* The checksums the beta 9 engine (insertion sort, every pair compared)
 * gives for these inputs: the faster engine must give the same results. */
static const uint32_t expected[] = {
    0xefcf5e1bu, 0x4cbc7653u, 0x9bfba06eu, 0xc263de5fu, 0xcbafc84eu, 0xfd1ce28cu};

int main(int argc, char** argv) {
    static const size_t sizes[] = {10, 100, 500, 1000, 2000, 5000};
    int mismatches = 0;
    int reps = argc > 1 ? atoi(argv[1]) : 5;
    printf("%6s %12s %12s %12s %12s %12s %12s  %s\n", "N", "dup_work_us", "sort_date",
           "sort_name", "sort_freq", "sort_proto", "search", "checksum");
    for(size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t n = sizes[si];
        RgDbEntry* entries = malloc(sizeof(RgDbEntry) * n);
        char* names = malloc(n * 24);
        uint16_t* view = malloc(sizeof(uint16_t) * n);
        uint16_t* scratch = malloc(sizeof(uint16_t) * n);
        (void)scratch;
        RgDb db;
        rg_db_init(&db, entries, n, names, n * 24);
        rng = 12345;
        for(size_t i = 0; i < n; i++) {
            char name[24];
            snprintf(name, sizeof(name), "cap_%05u.sub", (unsigned)(rnd() % 100000u));
            RgDbEntry* e = rg_db_add(&db, name);
            if(!e) break; // the name pool's 64 KB
            uint32_t r = rnd();
            e->mtime = 1760000000u + rnd() % 100000u;
            e->frequency = (r % 8) ? 300000000u + (r % 5) * 100000000u : 0;
            if(r % 3 == 0) {
                e->kind = RgDbKindRaw;
                e->size = 2000 + rnd() % (n * 4); // some sizes collide
                strcpy(e->protocol, "RAW");
            } else if(r % 17 == 1) {
                e->kind = RgDbKindCorrupt;
            } else {
                e->kind = RgDbKindProtocol;
                snprintf(e->protocol, sizeof(e->protocol), "%s", protos[rnd() % 12]);
                e->bits = 24;
                e->flags |= RG_DB_FLAG_HAS_KEY;
                e->hash = rnd() % (n / 2 + 1); // some keys repeat
                e->size = 300;
            }
        }
        uint64_t t_dup = UINT64_MAX, t_sort[4] = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX},
                 t_search = UINT64_MAX;
        uint32_t sum = 2166136261u;
        for(int rep = 0; rep < reps; rep++) {
            uint64_t t0 = now_ns();
#ifdef RG_DB_HAS_SCRATCH
            // As radiogeddon_db_load does now.
            size_t nc = rg_db_hash_candidates(&db, view);
            for(size_t i = 0; i < nc; i++)
                rg_db_set_content_hash(&db.entries[view[i]], db.entries[view[i]].size % 7u);
            rg_db_mark_duplicates(&db, scratch);
#else
            for(size_t i = 0; i < db.count; i++) {
                if(rg_db_needs_content_hash(&db, i))
                    rg_db_set_content_hash(&db.entries[i], db.entries[i].size % 7u);
            }
            rg_db_mark_duplicates(&db);
#endif
            uint64_t t1 = now_ns();
            if(t1 - t0 < t_dup) t_dup = t1 - t0;
            if(rep == 0) {
                for(size_t i = 0; i < db.count; i++)
                    sum = fold(fold(sum, db.entries[i].flags), db.entries[i].dup_count);
            }
            RgDbQuery q;
            memset(&q, 0, sizeof(q));
            q.show = RgDbShowAll;
            for(int s = 0; s < 4; s++) {
                q.sort = (RgDbSort)s;
                uint64_t a = now_ns();
                size_t k = rg_db_select(&db, &q, view, n);
                uint64_t b = now_ns();
                if(b - a < t_sort[s]) t_sort[s] = b - a;
                if(rep == 0)
                    for(size_t i = 0; i < k; i++)
                        sum = fold(sum, view[i]);
            }
            q.sort = RgDbSortDate;
            strcpy(q.search, "p_1");
            uint64_t a = now_ns();
            size_t k = rg_db_select(&db, &q, view, n);
            uint64_t b = now_ns();
            if(b - a < t_search) t_search = b - a;
            if(rep == 0)
                for(size_t i = 0; i < k; i++)
                    sum = fold(sum, view[i]);
        }
        n = db.count;
        printf("%6zu %12.1f %12.1f %12.1f %12.1f %12.1f %12.1f  %08x\n", n, t_dup / 1e3,
               t_sort[0] / 1e3, t_sort[1] / 1e3, t_sort[2] / 1e3, t_sort[3] / 1e3, t_search / 1e3,
               (unsigned)sum);
        if(sum != expected[si]) {
            printf("  checksum differs from the beta 9 engine's %08x\n", (unsigned)expected[si]);
            mismatches++;
        }
        free(entries);
        free(names);
        free(view);
        free(scratch);
    }
    return mismatches ? 1 : 0;
}
