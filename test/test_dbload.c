/**
 * Host-side tests for loading the Signal Database (helpers/radiogeddon_db.c)
 * from a real folder, through the stub Furi/Storage layer in test/stubs:
 * EXT_PATH maps to test/build/ext, the heap the loader sees is a knob, and
 * every allocation the loader makes is counted.
 *
 * Covers real .sub files (decoded and RAW), damaged and ignored entries,
 * duplicates, progress reporting, low-memory truncation and refusal, and that
 * repeated loads return every byte and close every file. All files are
 * synthetic. Build & run via `make -C test check`.
 */
#include "furi.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_db.h"
#include "../helpers/radiogeddon_storage.h"

#include <sys/stat.h>

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

/* What the loader leaves untouched for the screens after it (DB_HEAP_SPARE). */
#define SPARE (24u * 1024u)
/* Beyond its budget the loader holds a name buffer, a path and file handles. */
#define SLACK 1024u

static const char* k_princeton = "Filetype: Flipper SubGhz Key File\n"
                                 "Version: 1\n"
                                 "Frequency: 433920000\n"
                                 "Preset: FuriHalSubGhzPresetOok650Async\n"
                                 "Protocol: Princeton\n"
                                 "Bit: 24\n"
                                 "Key: 00 00 00 00 00 B4 DC 2A\n"
                                 "TE: 403\n";

static const char* k_came = "Filetype: Flipper SubGhz Key File\n"
                            "Version: 1\n"
                            "Frequency: 868350000\n"
                            "Preset: FuriHalSubGhzPresetOok650Async\n"
                            "Protocol: CAME\n"
                            "Bit: 12\n"
                            "Key: 00 00 00 00 00 00 0A BC\n";

static void write_file(const char* name, const void* data, size_t len) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    FILE* fp = fopen(path, "wb");
    if(!fp) {
        printf("  cannot write %s\n", path);
        exit(2);
    }
    fwrite(data, 1, len, fp);
    fclose(fp);
}

static void write_text(const char* name, const char* text) {
    write_file(name, text, strlen(text));
}

/* A RAW capture of about 2 KB (longer than the 512-byte head the index reads).
 * @p variant changes one duration without changing the size. */
static void write_raw(const char* name, int variant) {
    char buf[2400];
    int n = snprintf(
        buf,
        sizeof(buf),
        "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 315000000\n"
        "Preset: FuriHalSubGhzPresetOok270Async\nProtocol: RAW\n");
    for(int line = 0; line < 8; line++) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "RAW_Data:");
        for(int i = 0; i < 20; i++) {
            int high = (line == 7 && i == 19 && variant) ? 401 : 400;
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, " %d -1200", high);
        }
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "\n");
    }
    write_file(name, buf, (size_t)n);
}

static void make_folder(void) {
    if(system("rm -rf build/ext && mkdir -p " RADIOGEDDON_SIGNALS_FOLDER) != 0) exit(2);
}

static void fill_folder(void) {
    make_folder();
    write_text("gate.sub", k_princeton);
    write_text("gate copy.sub", k_princeton); /* same signal, other name */
    write_text("remote.SUB", k_came); /* extension in capitals */
    write_raw("raw_a.sub", 0);
    write_raw("raw_b.sub", 0); /* same capture */
    write_raw("raw_c.sub", 1); /* same size, different capture */
    static const uint8_t junk[] = {0x00, 0xFF, 0x13, 0x37, 'P', 'K', 0x03, 0x04};
    write_file("broken.sub", junk, sizeof(junk));
    write_text("empty.sub", "");
    /* Not indexed: other extensions, hidden files, folders. */
    write_text("notes.txt", "Filetype: Flipper SubGhz Key File\n");
    write_text(".hidden.sub", k_princeton);
    mkdir(RADIOGEDDON_SIGNALS_FOLDER "/folder.sub", 0755);
}
#define FILLED_FILES 8u

typedef struct {
    uint32_t calls;
    uint32_t last_done;
    uint32_t last_total;
    bool ordered;
} Progress;

static void on_progress(void* context, uint32_t done, uint32_t total) {
    Progress* p = context;
    if(done < p->last_done || done > total) p->ordered = false;
    p->calls++;
    p->last_done = done;
    p->last_total = total;
}

static const RgDbEntry* entry(const RadioGeddonDb* db, const char* name) {
    int32_t i = rg_db_find(&db->db, name);
    return i < 0 ? NULL : &db->db.entries[i];
}

static size_t count_view(RadioGeddonDb* db, RgDbShow show) {
    db->query.show = show;
    radiogeddon_db_apply(db);
    size_t n = db->view_count;
    db->query.show = RgDbShowAll;
    radiogeddon_db_apply(db);
    return n;
}

static void test_missing_folder(void) {
    printf("test_missing_folder\n");
    if(system("rm -rf build/ext") != 0) exit(2);
    stub_heap_free = 128 * 1024;
    RadioGeddonDbStatus status = RadioGeddonDbNoMemory;
    RadioGeddonDb* db = radiogeddon_db_load(NULL, &status, NULL, NULL);
    CHECK(db != NULL && status == RadioGeddonDbOk, "no folder: an empty Database, not an error");
    if(db) {
        CHECK(db->total_files == 0 && db->view_count == 0 && !db->truncated, "nothing listed");
        CHECK(radiogeddon_db_at(db, 0) == NULL, "no entry at 0");
    }
    radiogeddon_db_free(db);
    radiogeddon_db_free(NULL);
    CHECK(stub_live_bytes == 0 && stub_live_blocks == 0, "everything returned");
    CHECK(stub_files_open == 0, "nothing left open");
}

static void test_index(void) {
    printf("test_index\n");
    fill_folder();
    stub_heap_free = 128 * 1024;
    Progress p = {0, 0, 0, true};
    RadioGeddonDbStatus status;
    RadioGeddonDb* db = radiogeddon_db_load(NULL, &status, on_progress, &p);
    CHECK(db != NULL && status == RadioGeddonDbOk, "loads");
    if(!db) return;
    CHECK(db->total_files == FILLED_FILES, "only .sub files counted");
    CHECK(db->db.count == FILLED_FILES && !db->truncated, "all of them indexed");
    CHECK(entry(db, ".hidden.sub") == NULL && entry(db, "notes.txt") == NULL, "skipped files");
    CHECK(entry(db, "folder.sub") == NULL, "folders skipped");

    const RgDbEntry* gate = entry(db, "gate.sub");
    CHECK(gate && gate->kind == RgDbKindProtocol, "decoded file");
    CHECK(gate && strcmp(gate->protocol, "Princeton") == 0 && gate->bits == 24, "protocol, bits");
    CHECK(gate && gate->frequency == 433920000, "frequency");
    CHECK(gate && gate->size == strlen(k_princeton), "size from the listing");
    CHECK(gate && gate->mtime != 0, "time from the card");
    const RgDbEntry* remote = entry(db, "remote.SUB");
    CHECK(
        remote && remote->kind == RgDbKindProtocol && remote->frequency == 868350000, "capitals");
    const RgDbEntry* raw_a = entry(db, "raw_a.sub");
    CHECK(raw_a && raw_a->kind == RgDbKindRaw && raw_a->frequency == 315000000, "RAW file");
    CHECK(raw_a && raw_a->size > RG_DB_HEAD_BYTES, "RAW longer than the head read");
    const RgDbEntry* broken = entry(db, "broken.sub");
    const RgDbEntry* empty = entry(db, "empty.sub");
    CHECK(broken && broken->kind == RgDbKindCorrupt, "binary junk is damaged");
    CHECK(empty && empty->kind == RgDbKindCorrupt, "empty file is damaged");

    // Duplicates: by key for decoded files, by whole content for RAW.
    CHECK(gate && (gate->flags & RG_DB_FLAG_DUPLICATE) && gate->dup_count == 1, "key duplicate");
    CHECK(raw_a && (raw_a->flags & RG_DB_FLAG_DUPLICATE) && raw_a->dup_count == 1, "RAW twin");
    const RgDbEntry* raw_c = entry(db, "raw_c.sub");
    CHECK(raw_c && raw_c->size == raw_a->size, "same size as its neighbours");
    CHECK(raw_c && !(raw_c->flags & RG_DB_FLAG_DUPLICATE), "same size, other content: not a twin");
    CHECK(remote && !(remote->flags & RG_DB_FLAG_DUPLICATE), "unique file");

    CHECK(count_view(db, RgDbShowDuplicates) == 4, "four files in duplicate pairs");
    CHECK(count_view(db, RgDbShowDamaged) == 2, "two damaged files");
    CHECK(count_view(db, RgDbShowRaw) == 3, "three RAW files");
    CHECK(count_view(db, RgDbShowDecoded) == 3, "three decoded files");

    FuriString* path = furi_string_alloc();
    radiogeddon_db_path(db, gate, path);
    CHECK(
        strcmp(furi_string_get_cstr(path), RADIOGEDDON_SIGNALS_FOLDER "/gate.sub") == 0,
        "full path");
    furi_string_free(path);
    char when[16];
    radiogeddon_db_format_time(0, when, sizeof(when));
    CHECK(strcmp(when, "--") == 0, "unknown time");

    CHECK(p.calls == FILLED_FILES && p.ordered, "progress once per file, in order");
    CHECK(p.last_done == FILLED_FILES && p.last_total == FILLED_FILES, "progress ends at 100%");
    radiogeddon_db_free(db);
    CHECK(stub_live_bytes == 0 && stub_live_blocks == 0, "everything returned");
    CHECK(stub_files_open == 0, "every file closed");
}

static void test_low_memory(void) {
    printf("test_low_memory\n");
    fill_folder();
    RadioGeddonDbStatus status = RadioGeddonDbOk;
    stub_heap_free = SPARE;
    RadioGeddonDb* db = radiogeddon_db_load(NULL, &status, NULL, NULL);
    CHECK(db == NULL && status == RadioGeddonDbNoMemory, "no room past the spare: refused");
    CHECK(stub_live_bytes == 0 && stub_files_open == 0, "refusal leaves nothing behind");

    // Walk the heap up from the spare: every load stays inside its budget,
    // indexes more as memory grows, and says when it could not index all.
    size_t last_count = 0;
    bool in_budget = true, consistent = true, monotonic = true, cleaned = true;
    bool saw_refusal = false, saw_partial = false, saw_full = false;
    for(size_t heap = SPARE; heap <= SPARE + 2048; heap += 32) {
        stub_heap_free = heap;
        stub_alloc_reset_peak();
        db = radiogeddon_db_load(NULL, &status, NULL, NULL);
        if(stub_peak_bytes > heap - SPARE + SLACK) in_budget = false;
        if(!db) {
            saw_refusal = true;
            if(status != RadioGeddonDbNoMemory || last_count != 0) consistent = false;
        } else {
            if(status != RadioGeddonDbOk) consistent = false;
            if(db->truncated != (db->db.count < db->total_files)) consistent = false;
            if(db->view_count != db->db.count) consistent = false;
            if(db->db.count < last_count) monotonic = false;
            last_count = db->db.count;
            if(db->truncated) saw_partial = true;
            if(db->db.count == FILLED_FILES) saw_full = true;
        }
        radiogeddon_db_free(db);
        if(stub_live_bytes != 0 || stub_files_open != 0) cleaned = false;
    }
    CHECK(in_budget, "never uses more than the heap minus the spare");
    CHECK(consistent, "status and truncation flag match what was indexed");
    CHECK(monotonic, "more memory never indexes fewer files");
    CHECK(saw_refusal && saw_partial && saw_full, "refused, partial and full loads all seen");
    CHECK(cleaned, "every load returns its memory and closes its files");
}

static void test_repeated(void) {
    printf("test_repeated\n");
    fill_folder();
    stub_heap_free = 128 * 1024;
    size_t first_peak = 0;
    bool same = true, cleaned = true;
    for(int i = 0; i < 100; i++) {
        stub_alloc_reset_peak();
        RadioGeddonDbStatus status;
        RadioGeddonDb* db = radiogeddon_db_load(NULL, &status, NULL, NULL);
        if(!db || db->db.count != FILLED_FILES) same = false;
        if(db) {
            // Change the query the way the list screen does.
            db->query.sort = (RgDbSort)(i % RgDbSortCount);
            db->query.show = (RgDbShow)(i % RgDbShowCount);
            radiogeddon_db_apply(db);
        }
        radiogeddon_db_free(db);
        if(i == 0) first_peak = stub_peak_bytes;
        if(stub_peak_bytes != first_peak) same = false;
        if(stub_live_bytes != 0 || stub_live_blocks != 0 || stub_files_open != 0) cleaned = false;
    }
    CHECK(same, "100 loads: same result and same peak each time");
    CHECK(cleaned, "100 loads: nothing kept between them");
    CHECK(first_peak > 0 && first_peak < 4096, "eight files index in a few KB");
}

int main(void) {
    test_missing_folder();
    test_index();
    test_low_memory();
    test_repeated();
    if(system("rm -rf build/ext") != 0) return 2;

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("DBLOAD TESTS FAILED\n");
        return 1;
    }
    printf("ALL DBLOAD TESTS PASSED\n");
    return 0;
}
