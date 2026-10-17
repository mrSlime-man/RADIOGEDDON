#include "radiogeddon_safefile.h"

#include <furi.h>

#define TAG "RadioGeddonSafe"

static bool safe_exists(void* ctx, const char* path) {
    return storage_common_exists(ctx, path);
}

static bool safe_remove(void* ctx, const char* path) {
    FS_Error err = storage_common_remove(ctx, path);
    return err == FSE_OK || err == FSE_NOT_EXIST;
}

static bool safe_rename(void* ctx, const char* from, const char* to) {
    FS_Error err = storage_common_rename(ctx, from, to);
    if(err == FSE_EXIST) {
        // Firmware whose rename does not replace: the marker keeps the new
        // version (the source) safe while the destination is removed.
        if(!safe_remove(ctx, to)) return false;
        err = storage_common_rename(ctx, from, to);
    }
    return err == FSE_OK;
}

static bool safe_touch(void* ctx, const char* path) {
    File* file = storage_file_alloc(ctx);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    return ok;
}

static RgSafeFs safe_fs(Storage* storage) {
    RgSafeFs fs = {safe_exists, safe_remove, safe_rename, safe_touch, storage};
    return fs;
}

RgSafeRecover radiogeddon_safefile_recover(Storage* storage, const RgSafeFile* f) {
    RgSafeFs fs = safe_fs(storage);
    RgSafeRecover r = rg_safe_recover(&fs, f);
    // 1 dropped an unfinished save, 2 finished an interrupted one, 3 kept for retry.
    if(r != RgSafeClean) FURI_LOG_W(TAG, "%s: recovered (%d)", f->path, (int)r);
    return r;
}

bool radiogeddon_safefile_begin(Storage* storage, const RgSafeFile* f) {
    RgSafeFs fs = safe_fs(storage);
    return rg_safe_begin(&fs, f);
}

bool radiogeddon_safefile_commit(Storage* storage, const RgSafeFile* f) {
    RgSafeFs fs = safe_fs(storage);
    return rg_safe_commit(&fs, f);
}
