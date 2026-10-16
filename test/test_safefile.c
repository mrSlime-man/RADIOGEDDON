/* Host tests for rg_safefile: crash-safe replacement under simulated power loss.
 *
 * A tiny simulated card holds whole files as byte strings. Every primitive is
 * one step: creating, removing, and each 16-byte chunk of a write or of the
 * copy a rename makes. Rename is simulated three ways: as the Official
 * firmware does it (remove the destination, copy, delete the source), as an
 * atomic directory rename, and as a rename that refuses an existing
 * destination (the app's adapter then removes it first, as it does on the
 * device). A save is cut off at every step in turn; after the "reboot" the
 * loader recovers and reads the file, which must be exactly the old or exactly
 * the new version, with nothing left over. A second pass makes each step fail
 * once (an SD error) instead of losing power. */
#include "helpers/rg_safefile.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if(!(c)) {                                                       \
            failures++;                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
        }                                                                \
    } while(0)

#define MAX_FILES 8
#define MAX_LEN   512
#define CHUNK     16

typedef enum { RenameCopy, RenameAtomic, RenameNoReplace } RenameKind;

typedef struct {
    char name[32];
    uint8_t data[MAX_LEN];
    size_t len;
    int used;
} SimFile;

typedef struct {
    SimFile f[MAX_FILES];
    RenameKind rename;
    long step; /* steps done */
    long cut; /* power is lost when step reaches this (-1 never) */
    long fail_at; /* this step fails once (-1 never) */
    int dead; /* power lost: nothing more happens */
} Sim;

/* One step; false if it does not happen (power lost now, or an injected error). */
static int sim_step(Sim* s) {
    if(s->dead) return 0;
    if(s->cut >= 0 && s->step >= s->cut) {
        s->dead = 1;
        return 0;
    }
    long n = s->step++;
    return n != s->fail_at;
}

static SimFile* sim_find(Sim* s, const char* name) {
    for(int i = 0; i < MAX_FILES; i++)
        if(s->f[i].used && strcmp(s->f[i].name, name) == 0) return &s->f[i];
    return NULL;
}

static SimFile* sim_create(Sim* s, const char* name) {
    SimFile* f = sim_find(s, name);
    if(!f) {
        for(int i = 0; i < MAX_FILES && !f; i++)
            if(!s->f[i].used) f = &s->f[i];
        if(!f) return NULL;
        snprintf(f->name, sizeof(f->name), "%s", name);
        f->used = 1;
    }
    f->len = 0;
    return f;
}

static int sim_append(Sim* s, SimFile* f, const uint8_t* p, size_t n) {
    for(size_t done = 0; done < n; done += CHUNK) {
        size_t k = n - done < CHUNK ? n - done : CHUNK;
        if(!sim_step(s)) return 0;
        memcpy(f->data + f->len, p + done, k);
        f->len += k;
    }
    return 1;
}

static bool fs_exists(void* ctx, const char* path) {
    Sim* s = ctx;
    return !s->dead && sim_find(s, path) != NULL;
}

static bool fs_remove(void* ctx, const char* path) {
    Sim* s = ctx;
    SimFile* f = sim_find(s, path);
    if(!f) return !s->dead;
    if(!sim_step(s)) return false;
    f->used = 0;
    return true;
}

static bool fs_touch(void* ctx, const char* path) {
    Sim* s = ctx;
    if(!sim_step(s)) return false;
    return sim_create(s, path) != NULL;
}

static bool sim_rename_raw(Sim* s, const char* from, const char* to) {
    SimFile* src = sim_find(s, from);
    if(!src || s->dead) return false;
    if(s->rename == RenameAtomic) {
        if(!sim_step(s)) return false;
        SimFile* old = sim_find(s, to);
        if(old) old->used = 0;
        snprintf(src->name, sizeof(src->name), "%s", to);
        return true;
    }
    if(s->rename == RenameNoReplace && sim_find(s, to)) return false;
    // Official 1.4.3: remove the destination, copy, then delete the source.
    if(sim_find(s, to) && !fs_remove(s, to)) return false;
    if(!sim_step(s)) return false;
    SimFile* dst = sim_create(s, to);
    src = sim_find(s, from);
    if(!dst || !src) return false;
    uint8_t copy[MAX_LEN];
    size_t n = src->len;
    memcpy(copy, src->data, n);
    if(!sim_append(s, dst, copy, n)) return false;
    return fs_remove(s, from);
}

/* The app's adapter: a firmware that will not replace gets the destination
 * removed first (radiogeddon_storage's safe-file adapter does the same). */
static bool fs_rename(void* ctx, const char* from, const char* to) {
    Sim* s = ctx;
    if(sim_rename_raw(s, from, to)) return true;
    if(s->dead || s->rename != RenameNoReplace || !sim_find(s, to) || !sim_find(s, from))
        return false;
    return fs_remove(s, to) && sim_rename_raw(s, from, to);
}

static const RgSafeFile FILE_X = {"x.txt", "x.tmp", "x.cmt"};

static void sim_reset(Sim* s, RenameKind kind, const char* old) {
    memset(s, 0, sizeof(*s));
    s->rename = kind;
    s->cut = -1;
    s->fail_at = -1;
    if(old) {
        SimFile* f = sim_create(s, FILE_X.path);
        memcpy(f->data, old, strlen(old));
        f->len = strlen(old);
    }
}

/* The app's save: begin, write the .tmp, commit. */
static bool save(Sim* s, const RgSafeFs* fs, const char* text) {
    if(!rg_safe_begin(fs, &FILE_X)) return false;
    if(!sim_step(s)) return false;
    SimFile* f = sim_create(s, FILE_X.tmp);
    if(!f || !sim_append(s, f, (const uint8_t*)text, strlen(text))) {
        fs_remove(s, FILE_X.tmp);
        return false;
    }
    return rg_safe_commit(fs, &FILE_X);
}

static int content_is(Sim* s, const char* text) {
    SimFile* f = sim_find(s, FILE_X.path);
    if(!text) return f == NULL;
    return f && f->len == strlen(text) && memcmp(f->data, text, f->len) == 0;
}

static const char* OLD_TEXT =
    "Filetype: RadioGeddon Settings\nVersion: 1\nFrequency: 433920000\nPreset: 1\n"
    "Scan_dwell: 40\nThreshold: 10\nAnimation: 2\n";
static const char* NEW_TEXT =
    "Filetype: RadioGeddon Settings\nVersion: 1\nFrequency: 868350000\nPreset: 3\n"
    "Scan_dwell: 25\nThreshold: 12\nAnimation: 1\nLook: 1 2 3 4 5 6\n";

static void test_power_loss(RenameKind kind, const char* label, const char* old) {
    printf("test_power_loss (%s, %s)\n", label, old ? "replacing" : "first save");
    Sim s;
    RgSafeFs fs = {fs_exists, fs_remove, fs_rename, fs_touch, &s};
    // The steps of a whole save, then a cut at each of them.
    sim_reset(&s, kind, old);
    CHECK(save(&s, &fs, NEW_TEXT));
    long total = s.step;
    CHECK(content_is(&s, NEW_TEXT));
    int old_seen = 0, new_seen = 0, rolled = 0, bad = 0;
    for(long cut = 0; cut <= total; cut++) {
        sim_reset(&s, kind, old);
        s.cut = cut;
        bool ok = save(&s, &fs, NEW_TEXT);
        // Reboot: power back, the loader recovers before reading.
        s.dead = 0;
        s.cut = -1;
        RgSafeRecover r = rg_safe_recover(&fs, &FILE_X);
        if(r == RgSafeRolledForward) rolled++;
        int is_new = content_is(&s, NEW_TEXT), is_old = content_is(&s, old);
        if(!(is_new || is_old)) bad++;
        if(ok && !is_new) bad++;
        if(sim_find(&s, FILE_X.tmp) || sim_find(&s, FILE_X.marker)) bad++;
        old_seen += is_old && !is_new;
        new_seen += is_new;
        // Recovery is idempotent: a second boot changes nothing.
        CHECK(rg_safe_recover(&fs, &FILE_X) == RgSafeClean);
    }
    printf("  %ld cut points: old kept %d, new kept %d (%d rolled forward)\n",
           total + 1, old_seen, new_seen, rolled);
    CHECK(bad == 0);
    CHECK(old_seen > 0 && new_seen > 0);
    // A cut during the rename's copy is exactly what used to lose both.
    if(kind == RenameCopy) CHECK(rolled > 0);
}

static void test_write_errors(RenameKind kind, const char* label) {
    printf("test_write_errors (%s)\n", label);
    Sim s;
    RgSafeFs fs = {fs_exists, fs_remove, fs_rename, fs_touch, &s};
    sim_reset(&s, kind, OLD_TEXT);
    CHECK(save(&s, &fs, NEW_TEXT));
    long total = s.step;
    int bad = 0, failed = 0;
    for(long at = 0; at < total; at++) {
        sim_reset(&s, kind, OLD_TEXT);
        s.fail_at = at;
        bool ok = save(&s, &fs, NEW_TEXT);
        failed += !ok;
        // The app keeps running: the next load or save recovers.
        s.fail_at = -1;
        rg_safe_recover(&fs, &FILE_X);
        int is_new = content_is(&s, NEW_TEXT), is_old = content_is(&s, OLD_TEXT);
        if(!(is_new || is_old) || (ok && !is_new)) bad++;
        if(sim_find(&s, FILE_X.tmp) || sim_find(&s, FILE_X.marker)) bad++;
        // And a retried save then succeeds.
        if(!save(&s, &fs, NEW_TEXT) || !content_is(&s, NEW_TEXT)) bad++;
    }
    printf("  %ld failing steps, %d saves reported failure\n", total, failed);
    CHECK(bad == 0);
    CHECK(failed > 0);
}

static void test_stale_marker(void) {
    printf("test_stale_marker\n");
    Sim s;
    RgSafeFs fs = {fs_exists, fs_remove, fs_rename, fs_touch, &s};
    // A marker left without its .tmp (cut after the rename finished).
    sim_reset(&s, RenameCopy, OLD_TEXT);
    sim_create(&s, FILE_X.marker);
    CHECK(rg_safe_recover(&fs, &FILE_X) == RgSafeClean);
    CHECK(!sim_find(&s, FILE_X.marker));
    CHECK(content_is(&s, OLD_TEXT));
    // A half-written .tmp without a marker never replaces the file.
    SimFile* t = sim_create(&s, FILE_X.tmp);
    memcpy(t->data, "Filetype: Radio", 15);
    t->len = 15;
    CHECK(rg_safe_recover(&fs, &FILE_X) == RgSafeDiscarded);
    CHECK(content_is(&s, OLD_TEXT));
    // begin() refuses to write a .tmp while a commit cannot be finished.
    sim_reset(&s, RenameCopy, OLD_TEXT);
    sim_create(&s, FILE_X.marker);
    t = sim_create(&s, FILE_X.tmp);
    memcpy(t->data, NEW_TEXT, strlen(NEW_TEXT));
    t->len = strlen(NEW_TEXT);
    s.fail_at = 0; // the roll-forward's first step fails
    CHECK(!rg_safe_begin(&fs, &FILE_X));
    CHECK(sim_find(&s, FILE_X.tmp) && sim_find(&s, FILE_X.marker));
    s.fail_at = -1;
    CHECK(rg_safe_recover(&fs, &FILE_X) == RgSafeRolledForward);
    CHECK(content_is(&s, NEW_TEXT));
}

/* The protocol before rg_safefile (settings, journal, favorites, profiles):
 * write the .tmp, rename it over the file; the loader reads only the file and
 * the next save overwrites the .tmp. Shows the window the marker closes. */
static void test_previous_protocol(void) {
    printf("test_previous_protocol (copying rename)\n");
    Sim s;
    sim_reset(&s, RenameCopy, OLD_TEXT);
    s.step = 0;
    long total = 0;
    for(int pass = 0; pass < 2; pass++) {
        long lost = 0, cuts = pass ? total + 1 : 1;
        for(long cut = 0; cut < cuts; cut++) {
            sim_reset(&s, RenameCopy, OLD_TEXT);
            s.cut = pass ? cut : -1;
            SimFile* f = sim_step(&s) ? sim_create(&s, FILE_X.tmp) : NULL;
            if(f && sim_append(&s, f, (const uint8_t*)NEW_TEXT, strlen(NEW_TEXT)))
                fs_rename(&s, FILE_X.tmp, FILE_X.path);
            if(!pass) total = s.step;
            s.dead = 0;
            if(!(content_is(&s, NEW_TEXT) || content_is(&s, OLD_TEXT))) lost++;
        }
        if(pass) {
            printf("  %ld of %ld cut points leave neither version (old file gone)\n",
                   lost, total + 1);
            CHECK(lost > 0); // the defect rg_safefile fixes
        }
    }
}

int main(void) {
    test_previous_protocol();
    test_power_loss(RenameCopy, "copying rename", OLD_TEXT);
    test_power_loss(RenameCopy, "copying rename", NULL);
    test_power_loss(RenameAtomic, "atomic rename", OLD_TEXT);
    test_power_loss(RenameNoReplace, "non-replacing rename", OLD_TEXT);
    test_write_errors(RenameCopy, "copying rename");
    test_write_errors(RenameNoReplace, "non-replacing rename");
    test_stale_marker();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
