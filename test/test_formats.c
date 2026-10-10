/**
 * Host-side tests of RadioGeddon's file handling against the firmware's own
 * file format code and real .sub files:
 *
 *  - The Flipper Zero firmware's FlipperFormat and stream code (lib/
 *    flipper_format, lib/toolbox/stream) is compiled in unchanged, from the
 *    commit of the pinned Official release, over the stub Furi/Storage layer
 *    (files are real host files under test/build/ext).
 *  - The .sub files are the firmware's own Sub-GHz unit-test files, from the
 *    same commit, plus this repository's fixtures.
 *
 * Both are downloaded and checked by test/firmware/fetch.sh (`make -C test
 * formats` does it); they are not committed. The firmware's files are third-
 * party test data: nothing here was captured or checked on our hardware.
 *
 * Covered: loading every .sub file (signal details checked against an
 * independent reading of the same file, and against the Database index and
 * the RAW reader), the analyzer over every RAW file, settings saved and read
 * back, malformed settings, a save that fails part-way or cannot replace the
 * old file, writing decoded signals, unique names, and that all of it
 * returns every byte and file handle.
 */
#include "furi.h"
#include "furi_hal_rtc.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_storage.h"
#include "../helpers/radiogeddon_settings.h"
#include "../helpers/radiogeddon_bands.h"
#include "../helpers/rg_db.h"
#include "../helpers/rg_raw.h"
#include "../helpers/rg_analyzer.h"

#include <dirent.h>
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

/* The firmware's streams only check that a Storage is given; the stub layer
 * does not use it. */
static char card_record;
#define CARD ((Storage*)&card_record)

#define FW_SUBGHZ "build/fw/applications/debug/unit_tests/resources/unit_tests/subghz"

/* ---- An independent reading of a .sub file ------------------------------- */

typedef struct {
    char filetype[48];
    char protocol[64];
    char preset[64];
    bool has_protocol;
    uint32_t frequency;
    uint32_t bits;
    bool key8; /* Key line with exactly 8 bytes */
    uint64_t key;
    size_t raw_count;
    uint32_t raw_min;
    uint32_t raw_max;
    uint64_t raw_time;
} Expected;

static bool value_of(const char* line, const char* key, const char** value) {
    size_t n = strlen(key);
    if(strncmp(line, key, n) != 0 || line[n] != ':') return false;
    const char* v = line + n + 1;
    while(*v == ' ')
        v++;
    *value = v;
    return true;
}

static void copy_value(char* dst, size_t cap, const char* v) {
    size_t n = strcspn(v, "\r\n");
    if(n >= cap) n = cap - 1;
    memcpy(dst, v, n);
    dst[n] = '\0';
}

static bool read_expected(const char* path, Expected* e) {
    memset(e, 0, sizeof(*e));
    FILE* fp = fopen(path, "rb");
    if(!fp) return false;
    static char line[16384];
    while(fgets(line, sizeof(line), fp)) {
        const char* v;
        if(value_of(line, "Filetype", &v)) {
            copy_value(e->filetype, sizeof(e->filetype), v);
        } else if(value_of(line, "Frequency", &v)) {
            e->frequency = (uint32_t)strtoul(v, NULL, 10);
        } else if(value_of(line, "Preset", &v)) {
            copy_value(e->preset, sizeof(e->preset), v);
        } else if(value_of(line, "Protocol", &v)) {
            copy_value(e->protocol, sizeof(e->protocol), v);
            e->has_protocol = true;
        } else if(value_of(line, "Bit", &v)) {
            e->bits = (uint32_t)strtoul(v, NULL, 10);
        } else if(value_of(line, "Key", &v)) {
            unsigned b[9];
            int n = sscanf(
                v,
                "%x %x %x %x %x %x %x %x %x",
                &b[0],
                &b[1],
                &b[2],
                &b[3],
                &b[4],
                &b[5],
                &b[6],
                &b[7],
                &b[8]);
            e->key8 = n == 8;
            for(int i = 0; i < 8 && e->key8; i++)
                e->key = (e->key << 8) | b[i];
        } else if(value_of(line, "RAW_Data", &v)) {
            char* p = (char*)v;
            for(;;) {
                char* end;
                long long x = strtoll(p, &end, 10);
                if(end == p) break;
                p = end;
                if(*p == ',') p++; /* the firmware's RAW player allows "1, -2" */
                if(x == 0) continue;
                uint32_t a = (uint32_t)(x < 0 ? -x : x);
                e->raw_count++;
                e->raw_time += a;
                if(!e->raw_min || a < e->raw_min) e->raw_min = a;
                if(a > e->raw_max) e->raw_max = a;
            }
        }
    }
    fclose(fp);
    return true;
}

/* ---- Real .sub files ----------------------------------------------------- */

typedef struct {
    unsigned files, raw, decoded, by_encoding[RgEncodingCount];
    /* RAW captures with a decoded file of the same protocol (name_raw.sub and
     * name.sub): how often the analyzer's frame length equals its Bit. */
    unsigned paired, bits_exact, bits_one_short;
} Tally;

static void check_sub_file(const char* path, const char* name, Tally* t, bool verbose) {
    Expected e;
    if(!read_expected(path, &e)) {
        CHECK(false, "file readable");
        return;
    }
    t->files++;
    bool is_raw = e.has_protocol && strcmp(e.protocol, "RAW") == 0;
    char what[160];

    RadioGeddonLoadedSignal sig;
    radiogeddon_loaded_signal_init(&sig);
    bool ok = radiogeddon_storage_load(CARD, path, &sig);
    snprintf(what, sizeof(what), "%s: loads", name);
    CHECK(ok && sig.valid, what);
    RadioGeddonSignalKind kind = !e.has_protocol ? RadioGeddonSignalKindUnknown :
                                 is_raw          ? RadioGeddonSignalKindRaw :
                                                   RadioGeddonSignalKindProtocol;
    bool same = sig.kind == kind && sig.frequency == e.frequency &&
                strcmp(furi_string_get_cstr(sig.preset), e.preset) == 0 &&
                (!e.has_protocol || strcmp(furi_string_get_cstr(sig.protocol), e.protocol) == 0);
    snprintf(what, sizeof(what), "%s: kind, protocol, frequency, preset as in the file", name);
    CHECK(same, what);
    if(kind == RadioGeddonSignalKindProtocol) {
        t->decoded++;
        snprintf(what, sizeof(what), "%s: bit count and key as in the file", name);
        CHECK(sig.bit_count == e.bits && sig.key == (e.key8 ? e.key : 0), what);
    } else if(is_raw) {
        snprintf(what, sizeof(what), "%s: RAW sample count, min and max as in the file", name);
        CHECK(
            sig.raw_sample_count == e.raw_count && sig.raw_min_us == e.raw_min &&
                sig.raw_max_us == e.raw_max,
            what);
    }
    radiogeddon_loaded_signal_reset(&sig);

    /* The Database index reads the first 512 bytes on its own. */
    FILE* fp = fopen(path, "rb");
    char head[512];
    size_t len = fp ? fread(head, 1, sizeof(head), fp) : 0;
    bool whole = fp && fgetc(fp) == EOF;
    if(fp) fclose(fp);
    RgDbEntry entry;
    memset(&entry, 0, sizeof(entry));
    rg_db_parse_header(&entry, head, len, whole);
    RgDbKind dkind = !e.has_protocol ? RgDbKindUnknown : is_raw ? RgDbKindRaw : RgDbKindProtocol;
    char proto[RG_DB_PROTO_MAX]; /* the index keeps the first 15 characters */
    size_t plen = strnlen(e.protocol, sizeof(proto) - 1);
    memcpy(proto, e.protocol, plen);
    proto[plen] = '\0';
    snprintf(what, sizeof(what), "%s: Database index agrees", name);
    CHECK(
        entry.kind == dkind && entry.frequency == e.frequency &&
            (dkind != RgDbKindProtocol ||
             (strcmp(entry.protocol, proto) == 0 && entry.bits == e.bits)),
        what);

    if(!is_raw) return;
    t->raw++;

    /* The streaming reader and the analyzer, as Unknown Protocol Analysis
     * runs them on the device. */
    RadioGeddonRawFile* raw = radiogeddon_storage_raw_open(CARD, path);
    snprintf(what, sizeof(what), "%s: RAW file opens", name);
    CHECK(raw != NULL, what);
    if(!raw) return;
    static RgAnalyzer a;
    rg_analyzer_begin(&a);
    int32_t buf[128];
    size_t n;
    size_t count = 0;
    do {
        rg_raw_reader_rewind(&raw->reader);
        count = 0;
        while((n = rg_raw_reader_read(&raw->reader, buf, 128)) > 0) {
            count += n;
            rg_analyzer_feed(&a, buf, n);
        }
    } while(rg_analyzer_next_pass(&a));
    snprintf(what, sizeof(what), "%s: RAW reader returns every sample, none corrupt", name);
    CHECK(count == e.raw_count && raw->reader.time_us == e.raw_time && !raw->reader.corrupt, what);
    radiogeddon_storage_raw_close(raw);

    const RgAnalysis* r = &a.result;
    snprintf(what, sizeof(what), "%s: analysis figures in range", name);
    CHECK(
        r->sample_count <= count && r->noise_pct <= 100 && r->encoding_confidence <= 95 &&
            r->frames_kept <= r->frame_count &&
            (r->group_count == 0 || r->groups[0].frame < r->frames_kept) &&
            strlen(r->bits) == (r->group_count ? r->frames[r->groups[0].frame].bit_count : 0u),
        what);
    t->by_encoding[r->encoding]++;

    char key_path[512];
    size_t path_len = strlen(path);
    if(path_len > 8 && strcmp(path + path_len - 8, "_raw.sub") == 0) {
        snprintf(key_path, sizeof(key_path), "%.*s.sub", (int)(path_len - 8), path);
        Expected k;
        if(read_expected(key_path, &k) && k.bits > 0) {
            t->paired++;
            if(r->bit_count == k.bits) t->bits_exact++;
            if(r->bit_count + 1 == k.bits) t->bits_one_short++;
        }
    }
    if(verbose) {
        printf(
            "    %-28s %6u samples  %-10s Te %4lu us  conf %2d%%  frames %u/%u  bits %u\n",
            name,
            (unsigned)count,
            rg_analyzer_encoding_name(r->encoding),
            (unsigned long)r->te_us,
            r->encoding_confidence,
            (unsigned)r->signal_frames,
            (unsigned)r->frame_count,
            (unsigned)r->bit_count);
    }
}

static int name_cmp(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static void check_folder(const char* folder, unsigned min_files, bool verbose, Tally* out) {
    DIR* dir = opendir(folder);
    CHECK(dir != NULL, "test files present (run test/firmware/fetch.sh)");
    if(!dir) return;
    static char names[256][96];
    size_t count = 0;
    struct dirent* d;
    while((d = readdir(dir)) != NULL && count < 256) {
        size_t len = strlen(d->d_name);
        if(len > 4 && len < sizeof(names[0]) && strcmp(d->d_name + len - 4, ".sub") == 0)
            memcpy(names[count++], d->d_name, len + 1);
    }
    closedir(dir);
    qsort(names, count, sizeof(names[0]), name_cmp);

    Tally t;
    memset(&t, 0, sizeof(t));
    size_t live = stub_live_bytes;
    for(size_t i = 0; i < count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", folder, names[i]);
        check_sub_file(path, names[i], &t, verbose);
    }
    CHECK(t.files >= min_files, "all expected files found");
    CHECK(stub_live_bytes == live && stub_files_open == 0, "every byte and file handle returned");
    printf(
        "  %u files: %u decoded, %u RAW (analyzer: %u PWM, %u PPM, %u Manchester, %u unknown)\n",
        t.files,
        t.decoded,
        t.raw,
        t.by_encoding[RgEncodingPWM],
        t.by_encoding[RgEncodingPPM],
        t.by_encoding[RgEncodingManchester],
        t.by_encoding[RgEncodingUnknown]);
    if(out) *out = t;
}

static void test_firmware_files(bool verbose) {
    printf("test_firmware_files (firmware unit-test .sub files)\n");
    Tally t;
    check_folder(FW_SUBGHZ, 85, verbose, &t);
    /* Measured, not a claim of correctness: the analyzer's encoding and bit
     * count are hypotheses. Where a protocol's last bit is told by its low
     * period, that period runs into the frame gap and the bit is lost. A
     * drop below these counts means a change
     * made the analyzer worse on real captures. */
    printf(
        "  frame length equals the decoded Bit for %u of %u paired captures, one short for %u\n",
        t.bits_exact,
        t.paired,
        t.bits_one_short);
    CHECK(t.paired >= 33, "33 captures paired with a decoded file");
    CHECK(t.bits_exact >= 8, "analyzer frame length still exact for at least 8");
    CHECK(t.bits_exact + t.bits_one_short >= 13, "and within one bit for at least 13");
}

static void test_fixture_files(void) {
    printf("test_fixture_files (test/fixtures)\n");
    check_folder("fixtures", 3, false, NULL);
}

/* ---- Settings ------------------------------------------------------------ */

static void reset_card(void) {
    if(system("rm -rf build/ext && mkdir -p " RADIOGEDDON_SIGNALS_FOLDER) != 0) exit(2);
    stub_write_fail_after = -1;
    stub_rename_keeps_target = false;
    stub_open_fails = false;
}

static void write_settings_text(const char* text) {
    FILE* fp = fopen(RADIOGEDDON_SETTINGS_PATH, "wb");
    if(!fp) exit(2);
    fputs(text, fp);
    fclose(fp);
}

static bool same_settings(const RadioGeddonSettings* a, const RadioGeddonSettings* b) {
    return a->frequency == b->frequency && a->preset_index == b->preset_index &&
           a->scan_mask == b->scan_mask && a->scan_dwell_ms == b->scan_dwell_ms &&
           a->scan_threshold_db == b->scan_threshold_db &&
           a->scan_hold_on_hit == b->scan_hold_on_hit && a->hop_mask == b->hop_mask &&
           a->hop_dwell_ms == b->hop_dwell_ms && a->hop_hold_ms == b->hop_hold_ms &&
           a->hop_auto_record == b->hop_auto_record && a->db_sort == b->db_sort &&
           a->radio_external == b->radio_external && a->ext_power == b->ext_power &&
           a->radio_heap == b->radio_heap && a->radio_heap_fw == b->radio_heap_fw;
}

static RadioGeddonSettings custom_settings(void) {
    RadioGeddonSettings s;
    radiogeddon_settings_default(&s);
    s.frequency = 868350000;
    s.preset_index = 3;
    s.scan_mask = 0x5;
    s.scan_dwell_ms = 25;
    s.scan_threshold_db = 18;
    s.scan_hold_on_hit = true;
    s.hop_mask = 0x400;
    s.hop_dwell_ms = 500;
    s.hop_hold_ms = 4000;
    s.hop_auto_record = true;
    s.db_sort = RgDbSortProtocol;
    s.radio_external = true;
    s.ext_power = false;
    s.radio_heap = 27000;
    s.radio_heap_fw = 0x1234ABCDu;
    return s;
}

static void test_settings_round_trip(void) {
    printf("test_settings_round_trip\n");
    reset_card();
    RadioGeddonSettings def, got;
    radiogeddon_settings_default(&def);
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "no file: defaults");

    RadioGeddonSettings want = custom_settings();
    CHECK(radiogeddon_settings_save(CARD, &want), "save succeeds");
    CHECK(!storage_common_exists(CARD, RADIOGEDDON_SETTINGS_TEMP), "temporary file gone");
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &want), "every field read back");

    FILE* fp = fopen(RADIOGEDDON_SETTINGS_PATH, "rb");
    char text[1024] = {0};
    if(fp) {
        size_t n = fread(text, 1, sizeof(text) - 1, fp);
        text[n] = '\0';
        fclose(fp);
    }
    const char* head = "Filetype: RadioGeddon Settings\nVersion: 1\nFrequency: 868350000\n";
    CHECK(strncmp(text, head, strlen(head)) == 0, "file written in Flipper Format");
    CHECK(
        strstr(text, "Ext_5V: false\n") && strstr(text, "Radio_heap_fw: 305441741\n"),
        "booleans and numbers as the firmware writes them");

    /* Saving again replaces the file. */
    want.frequency = 315000000;
    CHECK(radiogeddon_settings_save(CARD, &want), "second save succeeds");
    radiogeddon_settings_load(CARD, &got);
    CHECK(got.frequency == 315000000, "second save replaced the first");
}

static void test_settings_malformed(void) {
    printf("test_settings_malformed\n");
    RadioGeddonSettings def, got;
    radiogeddon_settings_default(&def);

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 1\n"
                        "Frequency: 433000000\nPreset: 4\nScan_mask: 0\nScan_dwell_ms: 0\n"
                        "Scan_threshold_db: 61\nHop_dwell_ms: 49\nHop_hold_ms: 60001\n"
                        "Db_sort: 4\nScan_hold_on_hit: maybe\nHop_mask: 0\n"
                        "Radio_heap: 300000\nRadio_heap_fw: 7\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "every out-of-range value falls back to its default");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 1\n"
                        "Scan_dwell_ms: 1000\nScan_threshold_db: 1\nHop_dwell_ms: 10000\n"
                        "Hop_hold_ms: 100\nScan_mask: 4294967295\n");
    radiogeddon_settings_load(CARD, &got);
    uint32_t all = radiogeddon_frequencies_count >= 32 ?
                       0xFFFFFFFFu :
                       (1u << radiogeddon_frequencies_count) - 1u;
    CHECK(
        got.scan_dwell_ms == 1000 && got.scan_threshold_db == 1 && got.hop_dwell_ms == 10000 &&
            got.hop_hold_ms == 100,
        "limits themselves are accepted");
    CHECK(got.scan_mask == all, "mask bits beyond the frequency list dropped");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 1\n"
                        "Radio_heap_fw: 99\nHop_auto_record: true\nRadio_heap: 20000\n"
                        "Frequency: 315000000\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(
        got.frequency == 315000000 && got.hop_auto_record && got.radio_heap == 20000 &&
            got.radio_heap_fw == 99,
        "keys in any order; missing keys keep defaults");
    CHECK(got.hop_hold_ms == def.hop_hold_ms, "an absent key keeps its default");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 1\nRadio_heap: 20000\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(got.radio_heap == 0 && got.radio_heap_fw == 0, "a session cost without its firmware");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\r\nVersion: 1\r\nFrequency: 315000000\r\n"
                        "Hop_auto_record: true\r\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(got.frequency == 315000000 && got.hop_auto_record, "CRLF line ends");

    reset_card();
    write_settings_text("Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 315000000\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "another file type: defaults");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 2\nFrequency: 315000000\n");
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "another version: defaults");

    reset_card();
    write_settings_text("Filetype: RadioGeddon Settings\nVersion: 1\nFrequency: 315000000\n"
                        "Hop_dwell_ms: 5");
    radiogeddon_settings_load(CARD, &got);
    CHECK(got.frequency == 315000000, "a file cut off mid-line keeps the complete fields");

    reset_card();
    FILE* fp = fopen(RADIOGEDDON_SETTINGS_PATH, "wb");
    for(int i = 0; fp && i < 4096; i++)
        fputc((i * 7919) & 0xFF, fp);
    if(fp) fclose(fp);
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "binary garbage: defaults");

    reset_card();
    write_settings_text("");
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &def), "empty file: defaults");
    CHECK(stub_files_open == 0, "no file left open");
}

static void test_settings_failed_save(void) {
    printf("test_settings_failed_save\n");
    reset_card();
    RadioGeddonSettings old = custom_settings();
    CHECK(radiogeddon_settings_save(CARD, &old), "first save");

    RadioGeddonSettings next = custom_settings();
    next.frequency = 315000000;
    next.hop_dwell_ms = 300;
    stub_write_fail_after = 60; /* the card fails inside the new file */
    CHECK(!radiogeddon_settings_save(CARD, &next), "a failing card reports the save failed");
    stub_write_fail_after = -1;
    CHECK(!storage_common_exists(CARD, RADIOGEDDON_SETTINGS_TEMP), "partial file removed");
    RadioGeddonSettings got;
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &old), "the previous settings are kept intact");

    stub_rename_keeps_target = true; /* firmware that will not rename over a file */
    CHECK(radiogeddon_settings_save(CARD, &next), "save works where rename cannot replace");
    stub_rename_keeps_target = false;
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &next), "the new settings were swapped in");
    CHECK(!storage_common_exists(CARD, RADIOGEDDON_SETTINGS_TEMP), "no temporary file left");

    stub_open_fails = true;
    CHECK(!radiogeddon_settings_save(CARD, &old), "no card: save fails");
    stub_open_fails = false;
    radiogeddon_settings_load(CARD, &got);
    CHECK(same_settings(&got, &next), "and changes nothing");
}

/* ---- Saving signals ------------------------------------------------------ */

static void test_write_signal(void) {
    printf("test_write_signal\n");
    reset_card();
    FuriString* text = furi_string_alloc();
    furi_string_set(
        text,
        "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
        "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: Princeton\nBit: 24\n"
        "Key: 00 00 00 00 00 B4 DC 2A\nTE: 403\n");
    FuriString* path = furi_string_alloc();

    CHECK(radiogeddon_storage_make_unique_path(CARD, path, "gate"), "free name");
    CHECK(
        strcmp(furi_string_get_cstr(path), RADIOGEDDON_SIGNALS_FOLDER "/gate.sub") == 0,
        "path in the signals folder");
    CHECK(
        radiogeddon_storage_write_serialized(CARD, furi_string_get_cstr(path), text),
        "decoded signal written");
    RadioGeddonLoadedSignal sig;
    radiogeddon_loaded_signal_init(&sig);
    CHECK(
        radiogeddon_storage_load(CARD, furi_string_get_cstr(path), &sig) &&
            sig.kind == RadioGeddonSignalKindProtocol && sig.key == 0xB4DC2A &&
            sig.bit_count == 24 && strcmp(furi_string_get_cstr(sig.name), "gate") == 0,
        "and read back by the firmware's parser");
    radiogeddon_loaded_signal_reset(&sig);

    CHECK(radiogeddon_storage_make_unique_path(CARD, path, "gate"), "taken name gets a suffix");
    CHECK(
        strcmp(furi_string_get_cstr(path), RADIOGEDDON_SIGNALS_FOLDER "/gate_2.sub") == 0,
        "gate_2.sub");
    for(int i = 2; i <= 99; i++) {
        char p[128];
        snprintf(p, sizeof(p), RADIOGEDDON_SIGNALS_FOLDER "/gate_%d.sub", i);
        FILE* fp = fopen(p, "wb");
        if(fp) fclose(fp);
    }
    CHECK(!radiogeddon_storage_make_unique_path(CARD, path, "gate"), "gives up after _99");

    stub_write_fail_after = 30;
    CHECK(
        !radiogeddon_storage_write_serialized(CARD, RADIOGEDDON_SIGNALS_FOLDER "/cut.sub", text),
        "a failing card reports the save failed");
    stub_write_fail_after = -1;
    CHECK(
        !storage_common_exists(CARD, RADIOGEDDON_SIGNALS_FOLDER "/cut.sub"),
        "and leaves no damaged file");

    radiogeddon_storage_default_name(path);
    CHECK(strcmp(furi_string_get_cstr(path), "RG_20261009_123456") == 0, "default name");

    CHECK(
        radiogeddon_storage_raw_open(CARD, RADIOGEDDON_SIGNALS_FOLDER "/none.sub") == NULL,
        "missing RAW file: NULL");
    radiogeddon_loaded_signal_init(&sig);
    CHECK(
        !radiogeddon_storage_load(CARD, RADIOGEDDON_SIGNALS_FOLDER "/none.sub", &sig) &&
            !sig.valid,
        "missing file does not load");
    radiogeddon_loaded_signal_reset(&sig);
    furi_string_free(text);
    furi_string_free(path);
}

/* ---- Lifecycle ----------------------------------------------------------- */

static void test_repeated(void) {
    printf("test_repeated\n");
    reset_card();
    RadioGeddonSettings s = custom_settings(), got;
    size_t live = stub_live_bytes;
    stub_alloc_reset_peak();
    for(int i = 0; i < 100; i++) {
        s.hop_dwell_ms = (uint16_t)(100 + i);
        radiogeddon_settings_save(CARD, &s);
        radiogeddon_settings_load(CARD, &got);
        RadioGeddonLoadedSignal sig;
        radiogeddon_loaded_signal_init(&sig);
        radiogeddon_storage_load(CARD, "fixtures/raw_ref.sub", &sig);
        radiogeddon_loaded_signal_reset(&sig);
    }
    CHECK(got.hop_dwell_ms == 199, "last save read back");
    CHECK(stub_live_bytes == live, "100 saves and loads return every byte");
    CHECK(stub_files_open == 0, "and every file handle");
    printf("  peak while saving and loading: %u bytes\n", (unsigned)(stub_peak_bytes - live));
}

int main(int argc, char** argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    test_firmware_files(verbose);
    test_fixture_files();
    test_settings_round_trip();
    test_settings_malformed();
    test_settings_failed_save();
    test_write_signal();
    test_repeated();
    if(system("rm -rf build/ext") != 0) return 2;
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
