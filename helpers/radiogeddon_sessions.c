#include "radiogeddon_sessions.h"

#if RG_FEATURE_SESSIONS

#include <furi_hal_rtc.h>
#include <toolbox/path.h>

#define TAG "RadioGeddonSessions"

#define SESSIONS_ACTIVE RADIOGEDDON_SESSIONS_FOLDER "/active"

/* ---- RgSessionFs over the Storage API ---------------------------------------- */

static bool rs_exists(void* ctx, const char* path) {
    return storage_common_stat(ctx, path, NULL) == FSE_OK;
}

static bool rs_remove(void* ctx, const char* path) {
    return storage_common_remove(ctx, path) == FSE_OK;
}

static bool rs_rename(void* ctx, const char* from, const char* to) {
    return storage_common_rename(ctx, from, to) == FSE_OK;
}

static bool rs_write(void* ctx, const char* path, const char* data, size_t len) {
    File* file = storage_file_alloc(ctx);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, data, len) == len;
    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    return ok;
}

static long rs_read(void* ctx, const char* path, char* buf, size_t max) {
    File* file = storage_file_alloc(ctx);
    long n = -1;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        n = (long)storage_file_read(file, buf, max);
    }
    storage_file_close(file);
    storage_file_free(file);
    return n;
}

static RgSessionFs radiogeddon_sessions_fs(Storage* storage) {
    RgSessionFs fs = {rs_exists, rs_remove, rs_rename, rs_write, rs_read, storage};
    return fs;
}

static void radiogeddon_session_path(char* out, size_t size, const char* name) {
    snprintf(out, size, "%s/%s.txt", RADIOGEDDON_SESSIONS_FOLDER, name);
}

static void radiogeddon_sessions_ensure(Storage* storage) {
    radiogeddon_storage_ensure_paths(storage);
    storage_common_mkdir(storage, RADIOGEDDON_SESSIONS_FOLDER);
}

/* ---- list ------------------------------------------------------------------- */

void radiogeddon_sessions_list(Storage* storage, RadioGeddonSessionList* out) {
    memset(out, 0, sizeof(*out));
    File* dir = storage_file_alloc(storage);
    char name[64];
    FileInfo info;
    if(storage_dir_open(dir, RADIOGEDDON_SESSIONS_FOLDER)) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            size_t n = strlen(name);
            if(n < 5 || strcmp(name + n - 4, ".txt") != 0) continue;
            name[n - 4] = '\0';
            if(!rg_session_name_valid(name)) continue;
            if(out->count == RADIOGEDDON_SESSIONS_MAX) {
                out->truncated = true;
                continue;
            }
            // Insert sorted (case-insensitive).
            size_t at = out->count;
            while(at > 0 && strcasecmp(out->name[at - 1], name) > 0) {
                memcpy(out->name[at], out->name[at - 1], RG_SESSION_NAME_MAX);
                at--;
            }
            strlcpy(out->name[at], name, RG_SESSION_NAME_MAX);
            out->count++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
}

/* ---- one session ------------------------------------------------------------- */

RgSessionLoad radiogeddon_session_load(Storage* storage, const char* name, RgSession* s) {
    char path[128];
    radiogeddon_session_path(path, sizeof(path), name);
    char* scratch = malloc(RG_SESSION_TEXT_MAX);
    RgSessionFs fs = radiogeddon_sessions_fs(storage);
    RgSessionLoad r = rg_session_load(&fs, path, s, scratch);
    free(scratch);
    if(r == RgSessionLoadRecovered) FURI_LOG_W(TAG, "%s restored from its backup", name);
    return r;
}

bool radiogeddon_session_save(Storage* storage, const RgSession* s) {
    radiogeddon_sessions_ensure(storage);
    char path[128];
    radiogeddon_session_path(path, sizeof(path), s->name);
    char* scratch = malloc(RADIOGEDDON_SESSION_SCRATCH);
    RgSessionFs fs = radiogeddon_sessions_fs(storage);
    bool ok = rg_session_save(&fs, path, s, scratch);
    free(scratch);
    return ok;
}

bool radiogeddon_session_exists(Storage* storage, const char* name) {
    char path[128];
    radiogeddon_session_path(path, sizeof(path), name);
    return rs_exists(storage, path);
}

bool radiogeddon_session_rename(Storage* storage, RgSession* s, const char* to) {
    if(!rg_session_name_valid(to)) return false;
    // Same name but other letter case: FAT sees one file; just rewrite it.
    bool case_only = strcasecmp(s->name, to) == 0;
    if(!case_only && radiogeddon_session_exists(storage, to)) return false;
    char old[RG_SESSION_NAME_MAX];
    strlcpy(old, s->name, sizeof(old));
    // Deleting the old file clears the active mark: read it first.
    char active[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(storage, active, sizeof(active));
    strlcpy(s->name, to, sizeof(s->name));
    // The new file is complete before the old one goes.
    if(!case_only) {
        if(!radiogeddon_session_save(storage, s)) {
            strlcpy(s->name, old, sizeof(s->name));
            return false;
        }
        radiogeddon_session_delete(storage, old);
    } else {
        char from[128], tmp[140], dst[128];
        radiogeddon_session_path(from, sizeof(from), old);
        snprintf(tmp, sizeof(tmp), "%s.case", from);
        radiogeddon_session_path(dst, sizeof(dst), to);
        if(!rs_rename(storage, from, tmp) || !rs_rename(storage, tmp, dst)) {
            rs_rename(storage, tmp, from);
            strlcpy(s->name, old, sizeof(s->name));
            return false;
        }
        if(!radiogeddon_session_save(storage, s)) return false;
    }
    if(strcmp(active, old) == 0) radiogeddon_session_set_active(storage, to);
    return true;
}

bool radiogeddon_session_delete(Storage* storage, const char* name) {
    char path[128], other[140];
    radiogeddon_session_path(path, sizeof(path), name);
    snprintf(other, sizeof(other), "%s.bak", path);
    storage_common_remove(storage, other);
    snprintf(other, sizeof(other), "%s.tmp", path);
    storage_common_remove(storage, other);
    bool ok = storage_common_remove(storage, path) == FSE_OK;
    char active[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(storage, active, sizeof(active));
    if(strcmp(active, name) == 0) radiogeddon_session_set_active(storage, "");
    return ok;
}

/* ---- the active session -------------------------------------------------------- */

void radiogeddon_session_active(Storage* storage, char* out, size_t size) {
    char buf[RG_SESSION_NAME_MAX + 2];
    long n = rs_read(storage, SESSIONS_ACTIVE, buf, sizeof(buf) - 1);
    out[0] = '\0';
    if(n <= 0) return;
    buf[n] = '\0';
    for(char* c = buf; *c; c++) {
        if(*c == '\r' || *c == '\n') {
            *c = '\0';
            break;
        }
    }
    if(rg_session_name_valid(buf)) strlcpy(out, buf, size);
}

bool radiogeddon_session_set_active(Storage* storage, const char* name) {
    if(!name[0]) {
        storage_common_remove(storage, SESSIONS_ACTIVE);
        return true;
    }
    radiogeddon_sessions_ensure(storage);
    return rs_write(storage, SESSIONS_ACTIVE, name, strlen(name));
}

bool radiogeddon_session_capture_saved(Storage* storage, const char* file, char* out, size_t size) {
    out[0] = '\0';
    char active[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(storage, active, sizeof(active));
    if(!active[0]) return false;
    if(memmgr_heap_get_max_free_block() < sizeof(RgSession) + RADIOGEDDON_SESSION_SCRATCH + 2048u)
        return false;
    RgSession* s = malloc(sizeof(RgSession));
    bool ok = false;
    RgSessionLoad st = radiogeddon_session_load(storage, active, s);
    if(st == RgSessionLoadOk || st == RgSessionLoadRecovered) {
        RgSessionAdd add = rg_session_add(s, file);
        ok = add == RgSessionAddAlready ||
             (add == RgSessionAddOk && radiogeddon_session_save(storage, s));
    }
    if(ok) strlcpy(out, active, size);
    free(s);
    return ok;
}

void radiogeddon_sessions_rename_signal(Storage* storage, const char* from, const char* to) {
    if(memmgr_heap_get_max_free_block() <
       sizeof(RgSession) + sizeof(RadioGeddonSessionList) + RADIOGEDDON_SESSION_SCRATCH + 2048u)
        return; // the sessions then show the old name as missing
    RadioGeddonSessionList* list = malloc(sizeof(RadioGeddonSessionList));
    RgSession* s = malloc(sizeof(RgSession));
    radiogeddon_sessions_list(storage, list);
    for(size_t i = 0; i < list->count; i++) {
        RgSessionLoad st = radiogeddon_session_load(storage, list->name[i], s);
        if(st != RgSessionLoadOk && st != RgSessionLoadRecovered) continue;
        if(rg_session_rename_signal(s, from, to)) radiogeddon_session_save(storage, s);
    }
    free(s);
    free(list);
}

bool radiogeddon_session_signal_exists(Storage* storage, const char* file) {
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, file);
    return rs_exists(storage, path);
}

void radiogeddon_session_now(char* out, size_t size) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(out, size, "%04u-%02u-%02u %02u:%02u", dt.year, dt.month, dt.day, dt.hour, dt.minute);
}

#endif
