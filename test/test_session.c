/**
 * Host-side unit tests for research sessions (helpers/rg_session.c): the file
 * format, editing, the transactional save over an in-memory file system that
 * fails or "loses power" after any operation, recovery on load, and grouping
 * suggestions.
 * Run: make -C test
 */
#include "../helpers/rg_session.h"
#include <stdio.h>
#include <stdlib.h>
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

/* ---- in-memory file system ------------------------------------------------- */

#define FS_FILES 8
typedef struct {
    char path[128];
    char data[4096];
    long len;
    bool used;
} MemFile;

typedef struct {
    MemFile f[FS_FILES];
    int ops; // operations that changed something
    int power_after; // stop changing anything after this many (-1: never)
    bool fail_write;
    bool short_write; // writes lose their last byte (a bad card)
    int fail_rename; // fail the n-th rename (1-based, 0: never)
    int renames;
} MemFs;

static MemFile* mem_find(MemFs* m, const char* path) {
    for(int i = 0; i < FS_FILES; i++)
        if(m->f[i].used && strcmp(m->f[i].path, path) == 0) return &m->f[i];
    return NULL;
}

static bool mem_alive(MemFs* m) {
    if(m->power_after >= 0 && m->ops >= m->power_after) return false;
    m->ops++;
    return true;
}

static bool mem_exists(void* ctx, const char* path) {
    return mem_find(ctx, path) != NULL;
}
static bool mem_remove(void* ctx, const char* path) {
    MemFs* m = ctx;
    MemFile* f = mem_find(m, path);
    if(!f || !mem_alive(m)) return false;
    f->used = false;
    return true;
}
static bool mem_rename(void* ctx, const char* from, const char* to) {
    MemFs* m = ctx;
    MemFile* f = mem_find(m, from);
    if(!f || mem_find(m, to)) return false; // FAT: no overwrite
    m->renames++;
    if(m->fail_rename == m->renames) return false;
    if(!mem_alive(m)) return false;
    snprintf(f->path, sizeof(f->path), "%s", to);
    return true;
}
static bool mem_write(void* ctx, const char* path, const char* data, size_t len) {
    MemFs* m = ctx;
    if(m->fail_write || !mem_alive(m)) return false;
    MemFile* f = mem_find(m, path);
    for(int i = 0; !f && i < FS_FILES; i++)
        if(!m->f[i].used) f = &m->f[i];
    if(!f || len > sizeof(f->data)) return false;
    f->used = true;
    snprintf(f->path, sizeof(f->path), "%s", path);
    memcpy(f->data, data, len);
    f->len = (long)len - (m->short_write ? 1 : 0);
    return true;
}
static long mem_read(void* ctx, const char* path, char* buf, size_t max) {
    MemFile* f = mem_find(ctx, path);
    if(!f) return -1;
    long n = f->len < (long)max ? f->len : (long)max;
    memcpy(buf, f->data, (size_t)n);
    return n;
}

static MemFs g_mem;
static RgSessionFs g_fs = {mem_exists, mem_remove, mem_rename, mem_write, mem_read, &g_mem};
static char g_scratch[2 * RG_SESSION_TEXT_MAX];

static void mem_reset(void) {
    memset(&g_mem, 0, sizeof(g_mem));
    g_mem.power_after = -1;
}

#define PATH "/s/Garage.txt"

/* ---- tests ------------------------------------------------------------------ */

static void test_format(void) {
    printf("test_format\n");
    RgSession s;
    rg_session_init(&s, "Garage remote", "2026-10-11 12:00");
    CHECK(rg_session_add(&s, "btn1.sub") == RgSessionAddOk, "add");
    CHECK(rg_session_add(&s, "btn2.sub") == RgSessionAddOk, "add 2");
    CHECK(rg_session_add(&s, "btn1.sub") == RgSessionAddAlready, "no duplicates");
    CHECK(rg_session_add(&s, "notes.txt") == RgSessionAddInvalid, ".sub only");
    CHECK(rg_session_add(&s, "../x.sub") == RgSessionAddInvalid, "no paths");
    char text[RG_SESSION_TEXT_MAX];
    size_t n = rg_session_write(&s, text, sizeof(text));
    CHECK(n > 0, "written");
    CHECK(
        strcmp(
            text,
            "# RadioGeddon session\nVersion: 1\nName: Garage remote\n"
            "Created: 2026-10-11 12:00\nSignal: btn1.sub\nSignal: btn2.sub\n") == 0,
        "exact text");
    RgSession r;
    CHECK(rg_session_parse(&r, text, n) == RgSessionParseOk, "parse back");
    CHECK(strcmp(r.name, "Garage remote") == 0 && r.count == 2, "round trip");
    CHECK(
        strcmp(r.signal[1], "btn2.sub") == 0 && strcmp(r.created, "2026-10-11 12:00") == 0,
        "fields");
    // CRLF, unknown keys from a newer version, bad and duplicate lines.
    const char* crlf = "# RadioGeddon session\r\nVersion: 2\r\nName:  Spaced  \r\nColour: red\r\n"
                       "Signal: a.sub\r\nSignal: a.sub\r\nSignal: bad\r\nSignal: b.sub";
    CHECK(rg_session_parse(&r, crlf, strlen(crlf)) == RgSessionParseOk, "CRLF and extras");
    CHECK(strcmp(r.name, "Spaced") == 0 && r.count == 2, "trimmed, bad lines skipped");
    CHECK(
        rg_session_parse(&r, "Filetype: Flipper SubGhz\n", 25) == RgSessionParseNotSession,
        "not a session");
    CHECK(rg_session_parse(&r, "", 0) == RgSessionParseNotSession, "empty");
    const char* noname = "# RadioGeddon session\nVersion: 1\n";
    CHECK(rg_session_parse(&r, noname, strlen(noname)) == RgSessionParseNoName, "no name");
    // Too small a buffer: nothing half-written.
    CHECK(rg_session_write(&s, text, 40) == 0, "buffer too small");
    // Names.
    CHECK(rg_session_name_valid("Garage 1") && !rg_session_name_valid(""), "names");
    CHECK(
        !rg_session_name_valid("a/b") && !rg_session_name_valid("   ") &&
            !rg_session_name_valid(".x"),
        "bad names");
    CHECK(!rg_session_name_valid("0123456789012345678901234567890123"), "too long");
}

static void test_edit(void) {
    printf("test_edit\n");
    RgSession s;
    rg_session_init(&s, "S", NULL);
    char name[16];
    for(unsigned i = 0; i < RG_SESSION_SIGNALS; i++) {
        snprintf(name, sizeof(name), "r%u.sub", i);
        rg_session_add(&s, name);
    }
    CHECK(s.count == RG_SESSION_SIGNALS, "full");
    CHECK(rg_session_add(&s, "one_more.sub") == RgSessionAddFull, "refused when full");
    CHECK(rg_session_remove(&s, "r3.sub") && s.count == RG_SESSION_SIGNALS - 1, "removed");
    CHECK(rg_session_find(&s, "r3.sub") < 0 && rg_session_find(&s, "r4.sub") == 3, "order kept");
    CHECK(!rg_session_remove(&s, "r3.sub"), "not there");
    CHECK(rg_session_rename_signal(&s, "r4.sub", "door.sub"), "renamed");
    CHECK(rg_session_find(&s, "door.sub") == 3, "in place");
    CHECK(
        rg_session_rename_signal(&s, "r5.sub", "door.sub") && rg_session_find(&s, "r5.sub") < 0,
        "rename onto a listed name keeps one");
    CHECK(!rg_session_rename_signal(&s, "zz.sub", "y.sub"), "unknown");
}

static void test_save_and_recover(void) {
    printf("test_save_and_recover\n");
    RgSession v1, v2, got;
    rg_session_init(&v1, "Garage", "2026-10-11 12:00");
    rg_session_add(&v1, "a.sub");
    rg_session_init(&v2, "Garage", "2026-10-11 12:00");
    rg_session_add(&v2, "a.sub");
    rg_session_add(&v2, "b.sub");

    mem_reset();
    CHECK(rg_session_save(&g_fs, PATH, &v1, g_scratch), "first save");
    CHECK(
        rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadOk && got.count == 1,
        "load");
    CHECK(!mem_find(&g_mem, PATH ".tmp") && !mem_find(&g_mem, PATH ".bak"), "no leftovers");
    CHECK(rg_session_save(&g_fs, PATH, &v2, g_scratch), "update");
    CHECK(
        rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadOk && got.count == 2,
        "updated");

    // Power lost after every possible step of an update: the session loads,
    // complete, as either the old or the new version.
    int steps = 0;
    for(int cut = 0; cut < 12; cut++) {
        mem_reset();
        rg_session_save(&g_fs, PATH, &v1, g_scratch);
        g_mem.ops = 0;
        g_mem.power_after = cut;
        bool ok = rg_session_save(&g_fs, PATH, &v2, g_scratch);
        g_mem.power_after = -1;
        RgSessionLoad st = rg_session_load(&g_fs, PATH, &got, g_scratch);
        bool complete = (st == RgSessionLoadOk || st == RgSessionLoadRecovered) &&
                        (got.count == 1 || got.count == 2);
        char msg[64];
        snprintf(msg, sizeof(msg), "power cut after %d ops: complete session", cut);
        CHECK(complete, msg);
        if(ok) {
            snprintf(msg, sizeof(msg), "power cut after %d ops: saved means new", cut);
            CHECK(got.count == 2, msg);
        }
        CHECK(!mem_find(&g_mem, PATH ".tmp"), "no .tmp after load");
        if(ok) steps = cut;
    }
    CHECK(steps > 0, "save completes with enough power");

    // A write that fails, a bad card that loses data, a rename that fails:
    // the save reports failure and the old version stays.
    mem_reset();
    rg_session_save(&g_fs, PATH, &v1, g_scratch);
    g_mem.fail_write = true;
    CHECK(!rg_session_save(&g_fs, PATH, &v2, g_scratch), "write failure reported");
    g_mem.fail_write = false;
    CHECK(
        rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadOk && got.count == 1,
        "old kept");
    g_mem.short_write = true;
    CHECK(!rg_session_save(&g_fs, PATH, &v2, g_scratch), "read-back mismatch reported");
    g_mem.short_write = false;
    CHECK(
        rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadOk && got.count == 1,
        "old kept");
    for(int n = 1; n <= 2; n++) {
        g_mem.renames = 0;
        g_mem.fail_rename = n;
        CHECK(!rg_session_save(&g_fs, PATH, &v2, g_scratch), "rename failure reported");
        g_mem.fail_rename = 0;
        CHECK(
            rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadOk && got.count == 1,
            "old kept after a failed rename");
    }

    // A damaged file with a good backup is restored; with none it is "bad".
    mem_reset();
    rg_session_save(&g_fs, PATH, &v1, g_scratch);
    MemFile* f = mem_find(&g_mem, PATH);
    MemFile* b = &g_mem.f[FS_FILES - 1];
    *b = *f;
    snprintf(b->path, sizeof(b->path), PATH ".bak");
    memcpy(f->data, "garbage", 7);
    f->len = 7;
    CHECK(
        rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadRecovered && got.count == 1,
        "recovered");
    CHECK(mem_find(&g_mem, PATH) && !mem_find(&g_mem, PATH ".bak"), "backup moved back");
    f = mem_find(&g_mem, PATH);
    f->len = 3;
    CHECK(rg_session_load(&g_fs, PATH, &got, g_scratch) == RgSessionLoadBad, "bad without backup");
    CHECK(mem_find(&g_mem, PATH) != NULL, "a bad file is never deleted without a backup");
    CHECK(
        rg_session_load(&g_fs, "/s/none.txt", &got, g_scratch) == RgSessionLoadMissing, "missing");
}

static void test_suggest(void) {
    printf("test_suggest\n");
    // Time order. Two remotes on 433.92 (one Princeton 24 bit, one RAW), a
    // 868 sensor, and a later Princeton press after a long break.
    RgSessionItem it[] = {
        {"p1.sub", 433920000, "Princeton", 24, 1000},
        {"p2.sub", 433930000, "Princeton", 24, 1060}, // 10 kHz off: same
        {"raw1.sub", 433920000, "RAW", 0, 1100},
        {"p3.sub", 433920000, "Princeton", 24, 1200},
        {"raw2.sub", 433920000, "RAW", 0, 1300},
        {"s1.sub", 868350000, "RAW", 0, 1400},
        {"p4.sub", 433920000, "Princeton", 24, 1200 + RG_SESSION_GROUP_SECONDS + 600},
        {"x.sub", 0, "", 0, 0}, // no frequency
        {"k1.sub", 433920000, "Princeton", 12, 1250}, // other frame length
    };
    size_t n = sizeof(it) / sizeof(it[0]);
    int8_t g[16];
    RgSessionGroup groups[RG_SESSION_MAX_GROUPS];
    size_t count = rg_session_suggest(it, n, g, groups);
    CHECK(count == 2, "two groups");
    CHECK(
        groups[0].count == 3 && strcmp(groups[0].protocol, "Princeton") == 0,
        "Princeton x3 first");
    CHECK(groups[0].bits == 24 && groups[0].first == 0, "its bits and first file");
    CHECK(groups[1].count == 2 && strcmp(groups[1].protocol, "RAW") == 0, "RAW x2");
    CHECK(g[0] == 0 && g[1] == 0 && g[3] == 0, "members");
    CHECK(g[2] == 1 && g[4] == 1, "RAW members");
    CHECK(
        g[5] == -1 && g[6] == -1 && g[7] == -1 && g[8] == -1,
        "singles, late, unknown, other length");
    CHECK(rg_session_suggest(it, 0, g, groups) == 0, "no items");
    // A group whose first recording has no time: the next one still joins
    // (an unknown time never splits a group).
    RgSessionItem t[] = {
        {"a.sub", 315000000, "RAW", 0, 0},
        {"b.sub", 315000000, "RAW", 0, 5000},
        {"c.sub", 315000000, "RAW", 0, 5100},
    };
    CHECK(rg_session_suggest(t, 3, g, groups) == 1 && groups[0].count == 3, "unknown time joins");
}

int main(void) {
    test_format();
    test_edit();
    test_save_and_recover();
    test_suggest();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
