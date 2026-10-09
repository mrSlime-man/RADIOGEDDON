/**
 * Host-side tests for the signal database index (helpers/rg_db.c): header
 * parsing (decoded, RAW, damaged and cut-off files), duplicate detection,
 * filtering, search and every sort order. Build & run via
 * `make -C test check`. File contents are synthetic text.
 */
#include "../helpers/rg_db.h"
#include <stdio.h>
#include <string.h>

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

static const char* k_princeton = "Filetype: Flipper SubGhz Key File\n"
                                 "Version: 1\n"
                                 "Frequency: 433920000\n"
                                 "Preset: FuriHalSubGhzPresetOok650Async\n"
                                 "Protocol: Princeton\n"
                                 "Bit: 24\n"
                                 "Key: 00 00 00 00 00 B4 DC 2A\n"
                                 "TE: 403\n";

static const char* k_raw = "Filetype: Flipper SubGhz RAW File\r\n"
                           "Version: 1\r\n"
                           "Frequency: 315000000\r\n"
                           "Preset: FuriHalSubGhzPresetOok270Async\r\n"
                           "Protocol: RAW\r\n"
                           "RAW_Data: 400 -1200 400\r\n"
                           "Protocol: Fake\r\n";

static void parse(RgDbEntry* e, const char* text, bool whole) {
    memset(e, 0, sizeof(*e));
    rg_db_parse_header(e, text, strlen(text), whole);
}

static void test_parse(void) {
    printf("test_parse\n");
    RgDbEntry e, f;
    parse(&e, k_princeton, true);
    CHECK(e.kind == RgDbKindProtocol, "decoded file");
    CHECK(strcmp(e.protocol, "Princeton") == 0, "protocol name");
    CHECK(e.frequency == 433920000u && e.bits == 24, "frequency and bits");
    CHECK(e.flags & RG_DB_FLAG_HAS_KEY, "key seen");

    const char* lower = "Filetype: Flipper SubGhz Key File\nProtocol: Princeton\nBit: 24\n"
                        "Frequency: 433920000\nKey: 00 00 00 00 00 b4 dc 2a\n";
    parse(&f, lower, true);
    CHECK(f.hash == e.hash, "key hash ignores case");
    const char* spaced =
        "Filetype: Flipper SubGhz Key File\nProtocol: Princeton\nKey: 0000000000B4DC2A\n";
    parse(&f, spaced, true);
    CHECK(f.hash == e.hash, "key hash ignores spacing");

    parse(&e, k_raw, true);
    CHECK(e.kind == RgDbKindRaw && e.frequency == 315000000u, "RAW file with CRLF lines");
    CHECK(!(e.flags & RG_DB_FLAG_HAS_KEY) && e.hash == 0, "RAW has no key");
    CHECK(strcmp(e.protocol, "RAW") == 0, "header ends at RAW_Data (later lines ignored)");

    parse(&e, "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 4339x\n", true);
    CHECK(e.kind == RgDbKindUnknown, "no Protocol line: unknown");
    CHECK(e.frequency == 0, "garbage frequency read as unknown");

    const char* longp = "Filetype: Flipper SubGhz Key File\nProtocol: AVeryLongProtocolName2000\n";
    parse(&e, longp, true);
    CHECK(
        strlen(e.protocol) == RG_DB_PROTO_MAX - 1 && strncmp(e.protocol, "AVeryLong", 9) == 0,
        "long protocol name truncated");

    const char* cut = "Filetype: Flipper SubGhz Key File\nProtocol: CAME\nBit: 12\nKey: 00 00 0";
    parse(&e, cut, false);
    CHECK(
        e.kind == RgDbKindProtocol && !(e.flags & RG_DB_FLAG_HAS_KEY),
        "a cut-off last line is not trusted");
    parse(&e, cut, true);
    CHECK(e.flags & RG_DB_FLAG_HAS_KEY, "but used when it is the whole file");
}

static void test_damaged(void) {
    printf("test_damaged\n");
    RgDbEntry e;
    const char* cases[] = {
        "",
        "hello world\n",
        "Filetype: Flipper NFC Device\nProtocol: RAW\n",
        "Version: 1\nFiletype: Flipper SubGhz Key File\nProtocol: RAW\n",
        "\x01\x02\x03\xff garbage",
        "Filetype: Flipper SubGhz RAW File", /* no newline, not whole file */
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        parse(&e, cases[i], i != 5);
        CHECK(e.kind == RgDbKindCorrupt && e.protocol[0] == '\0', "damaged file");
    }
    parse(&e, "# comment first\nFiletype: Flipper SubGhz RAW File\nProtocol: RAW\n", true);
    CHECK(e.kind == RgDbKindRaw, "leading comment is fine");
}

static RgDbEntry g_entries[16];
static char g_names[256];

static RgDbEntry*
    add(RgDb* db, const char* name, const char* text, uint32_t mtime, uint32_t size) {
    RgDbEntry* e = rg_db_add(db, name);
    if(!e) return NULL;
    rg_db_parse_header(e, text, strlen(text), true);
    e->mtime = mtime;
    e->size = size;
    return e;
}

static void test_pool(void) {
    printf("test_pool\n");
    RgDb db;
    static RgDbEntry two[2];
    char pool[16];
    rg_db_init(&db, two, 2, pool, sizeof(pool));
    CHECK(rg_db_add(&db, "a.sub") != NULL, "first");
    CHECK(rg_db_add(&db, "much_too_long.sub") == NULL, "name pool full: refused");
    CHECK(rg_db_add(&db, "b.sub") != NULL, "second");
    CHECK(rg_db_add(&db, "c") == NULL, "entry table full: refused");
    CHECK(strcmp(rg_db_name(&db, &db.entries[1]), "b.sub") == 0 && db.count == 2, "names kept");
}

static void build(RgDb* db) {
    rg_db_init(db, g_entries, 16, g_names, sizeof(g_names));
    const char* came = "Filetype: Flipper SubGhz Key File\nFrequency: 433920000\nProtocol: CAME\n"
                       "Bit: 12\nKey: 00 00 00 00 00 00 0A BC\n";
    const char* came_868 = "Filetype: Flipper SubGhz Key File\nFrequency: 868350000\n"
                           "Protocol: CAME\nBit: 12\nKey: 00 00 00 00 00 00 0A BC\n";
    const char* raw_315 =
        "Filetype: Flipper SubGhz RAW File\nFrequency: 315000000\nProtocol: RAW\n";
    add(db, "Gate.sub", k_princeton, 300, 200); /* 0 */
    add(db, "gate_copy.sub", k_princeton, 100, 210); /* 1 dup of 0 */
    add(db, "came_A.sub", came, 200, 150); /* 2 */
    add(db, "came_868.sub", came_868, 250, 150); /* 3 not dup of 2 (freq) */
    add(db, "RG_1.sub", k_raw, 400, 5000); /* 4 */
    add(db, "RG_2.sub", raw_315, 500, 5000); /* 5 same size as 4 */
    add(db, "RG_3.sub", raw_315, 600, 7000); /* 6 */
    add(db, "broken.sub", "nonsense", 50, 10); /* 7 */
    add(db,
        "nofreq.sub",
        "Filetype: Flipper SubGhz Key File\nProtocol: Nice FLO\nKey: 01\n",
        700,
        90); /* 8 */
}

static void test_duplicates(void) {
    printf("test_duplicates\n");
    RgDb db;
    build(&db);
    CHECK(db.count == 9, "nine files");
    CHECK(
        rg_db_needs_content_hash(&db, 4) && rg_db_needs_content_hash(&db, 5),
        "RAW files sharing a size need a content hash");
    CHECK(!rg_db_needs_content_hash(&db, 6), "unique size: no hash needed");
    CHECK(!rg_db_needs_content_hash(&db, 0), "decoded files never need one");

    rg_db_mark_duplicates(&db);
    CHECK(!(db.entries[4].flags & RG_DB_FLAG_DUPLICATE), "same size alone is not a duplicate");

    rg_db_set_content_hash(&db.entries[4], 0x1234);
    rg_db_set_content_hash(&db.entries[5], 0x1234);
    rg_db_mark_duplicates(&db);
    CHECK(
        (db.entries[0].flags & RG_DB_FLAG_DUPLICATE) &&
            (db.entries[1].flags & RG_DB_FLAG_DUPLICATE),
        "same protocol, frequency, bits and key: duplicates");
    CHECK(db.entries[0].dup_count == 1, "one other copy");
    CHECK(
        !(db.entries[2].flags & RG_DB_FLAG_DUPLICATE) &&
            !(db.entries[3].flags & RG_DB_FLAG_DUPLICATE),
        "different frequency: not a duplicate");
    CHECK(
        (db.entries[4].flags & RG_DB_FLAG_DUPLICATE) &&
            (db.entries[5].flags & RG_DB_FLAG_DUPLICATE),
        "RAW with equal size and content hash: duplicates");
    CHECK(!(db.entries[7].flags & RG_DB_FLAG_DUPLICATE), "damaged files are never duplicates");

    rg_db_set_content_hash(&db.entries[5], 0x9999);
    rg_db_mark_duplicates(&db);
    CHECK(
        !(db.entries[4].flags & RG_DB_FLAG_DUPLICATE) && db.entries[4].dup_count == 0,
        "re-marking clears stale flags");
}

static bool order_is(const RgDb* db, const uint16_t* idx, size_t n, const char* const* names) {
    for(size_t i = 0; i < n; i++)
        if(strcmp(rg_db_name(db, &db->entries[idx[i]]), names[i]) != 0) return false;
    return true;
}

static void test_select(void) {
    printf("test_select\n");
    RgDb db;
    build(&db);
    rg_db_set_content_hash(&db.entries[4], 7);
    rg_db_set_content_hash(&db.entries[5], 7);
    rg_db_mark_duplicates(&db);
    uint16_t idx[16];
    RgDbQuery q;
    memset(&q, 0, sizeof(q));

    q.sort = RgDbSortDate;
    size_t n = rg_db_select(&db, &q, idx, 16);
    const char* by_date[] = {
        "nofreq.sub",
        "RG_3.sub",
        "RG_2.sub",
        "RG_1.sub",
        "Gate.sub",
        "came_868.sub",
        "came_A.sub",
        "gate_copy.sub",
        "broken.sub"};
    CHECK(n == 9 && order_is(&db, idx, n, by_date), "newest first");

    q.sort = RgDbSortName;
    n = rg_db_select(&db, &q, idx, 16);
    const char* by_name[] = {
        "broken.sub",
        "came_868.sub",
        "came_A.sub",
        "Gate.sub",
        "gate_copy.sub",
        "nofreq.sub",
        "RG_1.sub",
        "RG_2.sub",
        "RG_3.sub"};
    CHECK(order_is(&db, idx, n, by_name), "by name, ignoring case");

    q.sort = RgDbSortFrequency;
    n = rg_db_select(&db, &q, idx, 16);
    const char* by_freq[] = {
        "RG_1.sub",
        "RG_2.sub",
        "RG_3.sub",
        "came_A.sub",
        "Gate.sub",
        "gate_copy.sub",
        "came_868.sub",
        "broken.sub",
        "nofreq.sub"};
    CHECK(order_is(&db, idx, n, by_freq), "by frequency, unknown last, ties by name");

    q.sort = RgDbSortProtocol;
    n = rg_db_select(&db, &q, idx, 16);
    const char* by_proto[] = {
        "came_868.sub",
        "came_A.sub",
        "nofreq.sub",
        "Gate.sub",
        "gate_copy.sub",
        "RG_1.sub",
        "RG_2.sub",
        "RG_3.sub",
        "broken.sub"};
    CHECK(order_is(&db, idx, n, by_proto), "by protocol, damaged last");

    q.sort = RgDbSortName;
    q.show = RgDbShowRaw;
    CHECK(rg_db_select(&db, &q, idx, 16) == 3, "RAW only");
    q.show = RgDbShowDecoded;
    CHECK(rg_db_select(&db, &q, idx, 16) == 5, "decoded only");
    q.show = RgDbShowProtocol;
    strcpy(q.protocol, "came");
    CHECK(rg_db_select(&db, &q, idx, 16) == 2, "one protocol, any case");
    q.show = RgDbShowDuplicates;
    n = rg_db_select(&db, &q, idx, 16);
    const char* dups[] = {"Gate.sub", "gate_copy.sub", "RG_1.sub", "RG_2.sub"};
    CHECK(n == 4 && order_is(&db, idx, n, dups), "duplicates");
    q.show = RgDbShowDamaged;
    n = rg_db_select(&db, &q, idx, 16);
    CHECK(n == 1 && idx[0] == 7, "damaged");

    q.show = RgDbShowAll;
    strcpy(q.search, "GATE");
    n = rg_db_select(&db, &q, idx, 16);
    CHECK(n == 2, "search ignores case");
    strcpy(q.search, "rg_");
    q.show = RgDbShowRaw;
    CHECK(rg_db_select(&db, &q, idx, 16) == 3, "search and filter combine");
    strcpy(q.search, "zzz");
    CHECK(rg_db_select(&db, &q, idx, 16) == 0, "no match");
    q.search[0] = '\0';
    q.show = RgDbShowAll;
    CHECK(rg_db_select(&db, &q, idx, 4) == 4, "output limit respected");

    const char* protos[8];
    n = rg_db_protocols(&db, protos, 8);
    CHECK(
        n == 3 && strcmp(protos[0], "CAME") == 0 && strcmp(protos[1], "Nice FLO") == 0 &&
            strcmp(protos[2], "Princeton") == 0,
        "distinct protocol names, sorted");
    CHECK(rg_db_protocols(&db, protos, 1) == 1, "protocol list limit");
}

static void test_misc(void) {
    printf("test_misc\n");
    CHECK(rg_db_name_matches("RG_20261009.sub", ""), "empty search matches");
    CHECK(rg_db_name_matches("RG_20261009.sub", "1009.S"), "middle, any case");
    CHECK(!rg_db_name_matches("abc", "abcd"), "longer than the name");
    CHECK(rg_db_hash(RG_DB_HASH_INIT, "a", 1) == 0xE40C292Cu, "FNV-1a of \"a\"");
    uint32_t h = rg_db_hash(RG_DB_HASH_INIT, "ab", 1);
    CHECK(rg_db_hash(h, "b", 1) == rg_db_hash(RG_DB_HASH_INIT, "ab", 2), "hash continues");
    CHECK(strcmp(rg_db_sort_name(RgDbSortFrequency), "Freq") == 0, "sort label");
    CHECK(strcmp(rg_db_show_name(RgDbShowDamaged), "Damaged") == 0, "filter label");
}

int main(void) {
    test_parse();
    test_damaged();
    test_pool();
    test_duplicates();
    test_select();
    test_misc();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("DATABASE TESTS FAILED\n");
        return 1;
    }
    printf("ALL DATABASE TESTS PASSED\n");
    return 0;
}
