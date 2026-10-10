/**
 * Fuzz target: the Database index (helpers/rg_db.c) built from file names and
 * file contents of any kind, plus the RAW_Data line parser that Signal Info
 * uses (helpers/radiogeddon_dsp.c).
 *
 * Input: a record count, then that many records, each a name, the first
 * block of a "file" and its timestamp, cut from the bytes as they come; what
 * is left over after the last record is parsed as one RAW_Data line.
 *
 * Invariants (abort on violation):
 *  - parsed entries keep a known kind and a NUL-terminated protocol name;
 *  - every query returns valid, distinct indices of entries it should show,
 *    and "all" with no search shows every entry;
 *  - the protocol list is distinct and only holds decoded entries' names;
 *  - name checks, extension stripping and lookups stay inside their strings;
 *  - the line parser's counts, minimum and maximum agree.
 */
#include "../../helpers/rg_db.h"
#include "../../helpers/radiogeddon_dsp.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

#define MAX_ENTRIES 24
#define POOL        512

typedef struct {
    const uint8_t* p;
    size_t left;
} In;

static size_t take(In* in, size_t want, const uint8_t** out) {
    if(want > in->left) want = in->left;
    *out = in->p;
    in->p += want;
    in->left -= want;
    return want;
}

static uint32_t take_u32(In* in) {
    const uint8_t* b;
    size_t n = take(in, 4, &b);
    uint32_t v = 0;
    for(size_t i = 0; i < n; i++)
        v = (v << 8) | b[i];
    return v;
}

static void copy_str(char* dst, size_t cap, const uint8_t* src, size_t len) {
    if(len >= cap) len = cap - 1;
    size_t n = 0;
    for(size_t i = 0; i < len; i++)
        if(src[i] != '\0') dst[n++] = (char)src[i];
    dst[n] = '\0';
}

static bool shown_by(const RgDbEntry* e, RgDbShow show) {
    switch(show) {
    case RgDbShowRaw:
        return e->kind == RgDbKindRaw;
    case RgDbShowDecoded:
    case RgDbShowProtocol:
        return e->kind == RgDbKindProtocol;
    case RgDbShowDuplicates:
        return (e->flags & RG_DB_FLAG_DUPLICATE) != 0;
    case RgDbShowDamaged:
        return e->kind == RgDbKindCorrupt || e->kind == RgDbKindUnknown;
    default:
        return true;
    }
}

static void check_names(const char* name) {
    RgDbNameError err = rg_db_check_name(name);
    REQUIRE(err <= RgDbNameBadEdge);
    REQUIRE(rg_db_name_error_text(err) != NULL);
    if(err == RgDbNameOk) REQUIRE(name[0] != '\0' && strlen(name) <= RG_DB_NAME_MAX);
    char copy[128];
    strcpy(copy, name);
    size_t before = strlen(copy);
    rg_db_strip_ext(copy, ".sub");
    REQUIRE(strlen(copy) == before || strlen(copy) + 4 == before);
    REQUIRE(rg_db_name_matches(name, ""));
    REQUIRE(rg_db_name_matches(name, name));
}

static void check_line(In* in) {
    char line[1024];
    const uint8_t* b;
    size_t n = take(in, sizeof(line) - 1, &b);
    copy_str(line, sizeof(line), b, n);
    size_t count = 0, clusters_n = 0;
    uint32_t min_us = 0, max_us = 0;
    RadioGeddonCluster clusters[RADIOGEDDON_MAX_CLUSTERS];
    memset(clusters, 0, sizeof(clusters));
    size_t parsed = radiogeddon_dsp_parse_line(
        line, &count, &min_us, &max_us, clusters, &clusters_n, RADIOGEDDON_MAX_CLUSTERS);
    REQUIRE(parsed == count);
    REQUIRE(clusters_n <= RADIOGEDDON_MAX_CLUSTERS);
    REQUIRE(count == 0 ? (min_us == 0 && max_us == 0) : (min_us > 0 && min_us <= max_us));
    uint64_t in_clusters = 0;
    for(size_t i = 0; i < clusters_n; i++)
        in_clusters += clusters[i].count;
    REQUIRE(in_clusters <= count);
    radiogeddon_dsp_sort_clusters(clusters, clusters_n);
    for(size_t i = 1; i < clusters_n; i++)
        REQUIRE(clusters[i - 1].center <= clusters[i].center);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    In in = {data, size};
    static RgDbEntry entries[MAX_ENTRIES];
    static char pool[POOL];
    RgDb db;
    rg_db_init(&db, entries, MAX_ENTRIES, pool, POOL);

    if(size < 1) return 0;
    const uint8_t* b;
    take(&in, 1, &b);
    size_t records = b[0] % (MAX_ENTRIES + 1u);
    for(size_t r = 0; r < records && in.left >= 2; r++) {
        uint8_t head[2];
        take(&in, 2, &b);
        memcpy(head, b, 2);
        char name[80];
        size_t n = take(&in, head[0] % 72u, &b);
        copy_str(name, sizeof(name), b, n);
        check_names(name);

        RgDbEntry* e = rg_db_add(&db, name);
        if(!e) break;
        REQUIRE(strcmp(rg_db_name(&db, e), name) == 0);

        size_t len = take(&in, (size_t)(head[1] & 0x7fu) * 4u, &b);
        rg_db_parse_header(e, (const char*)b, len, (head[1] & 0x80u) != 0);
        REQUIRE(e->kind <= RgDbKindProtocol);
        REQUIRE(memchr(e->protocol, '\0', RG_DB_PROTO_MAX) != NULL);
        e->mtime = take_u32(&in);
        e->size = (uint32_t)len;
        size_t index = (size_t)(e - entries);
        if(rg_db_needs_content_hash(&db, index))
            rg_db_set_content_hash(e, rg_db_hash(2166136261u, b, len));
    }
    rg_db_mark_duplicates(&db);

    for(size_t i = 0; i < db.count; i++) {
        int32_t found = rg_db_find(&db, rg_db_name(&db, &entries[i]));
        REQUIRE(found >= 0 && (size_t)found < db.count);
        REQUIRE(strcmp(rg_db_name(&db, &entries[found]), rg_db_name(&db, &entries[i])) == 0);
        if(entries[i].flags & RG_DB_FLAG_DUPLICATE) REQUIRE(entries[i].dup_count > 0);
    }

    const char* protocols[MAX_ENTRIES];
    size_t np = rg_db_protocols(&db, protocols, MAX_ENTRIES);
    REQUIRE(np <= db.count);
    for(size_t i = 0; i < np; i++) {
        REQUIRE(strlen(protocols[i]) < RG_DB_PROTO_MAX);
        for(size_t k = 0; k < i; k++)
            REQUIRE(strcmp(protocols[k], protocols[i]) != 0);
    }

    RgDbQuery q;
    memset(&q, 0, sizeof(q));
    if(np) strcpy(q.protocol, protocols[0]);
    for(int sort = 0; sort < RgDbSortCount; sort++) {
        for(int show = 0; show < RgDbShowCount; show++) {
            for(int search = 0; search < 2; search++) {
                q.sort = (RgDbSort)sort;
                q.show = (RgDbShow)show;
                q.search[0] = '\0';
                if(search && db.count) {
                    strncpy(q.search, rg_db_name(&db, &entries[0]), RG_DB_SEARCH_MAX - 1);
                    q.search[RG_DB_SEARCH_MAX - 1] = '\0';
                }
                uint16_t out[MAX_ENTRIES];
                size_t got = rg_db_select(&db, &q, out, MAX_ENTRIES);
                REQUIRE(got <= db.count);
                bool seen[MAX_ENTRIES] = {false};
                for(size_t i = 0; i < got; i++) {
                    REQUIRE(out[i] < db.count && !seen[out[i]]);
                    seen[out[i]] = true;
                    REQUIRE(shown_by(&entries[out[i]], q.show));
                    REQUIRE(rg_db_name_matches(rg_db_name(&db, &entries[out[i]]), q.search));
                }
                if(show == RgDbShowAll && !search) REQUIRE(got == db.count);
            }
        }
        REQUIRE(rg_db_sort_name((RgDbSort)sort) != NULL);
    }

    check_line(&in);
    return 0;
}
