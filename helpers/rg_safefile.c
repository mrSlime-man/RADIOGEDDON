#include "rg_safefile.h"

RgSafeRecover rg_safe_recover(const RgSafeFs* fs, const RgSafeFile* f) {
    bool marker = fs->exists(fs->ctx, f->marker);
    bool tmp = fs->exists(fs->ctx, f->tmp);
    if(marker) {
        if(tmp) {
            // The .tmp is complete: put it in place (again, if a copy was cut off).
            if(!fs->rename(fs->ctx, f->tmp, f->path)) return RgSafeStuck;
        }
        // With no .tmp the rename finished (it deletes its source last).
        fs->remove(fs->ctx, f->marker);
        return tmp ? RgSafeRolledForward : RgSafeClean;
    }
    if(tmp) {
        fs->remove(fs->ctx, f->tmp);
        return RgSafeDiscarded;
    }
    return RgSafeClean;
}

bool rg_safe_begin(const RgSafeFs* fs, const RgSafeFile* f) {
    if(rg_safe_recover(fs, f) == RgSafeStuck) return false;
    // A stale marker would vouch for the .tmp about to be written.
    return !fs->exists(fs->ctx, f->marker) && fs->remove(fs->ctx, f->tmp);
}

bool rg_safe_commit(const RgSafeFs* fs, const RgSafeFile* f) {
    if(!fs->touch(fs->ctx, f->marker)) {
        // Without a marker nothing vouches for the .tmp: drop it, keep the file.
        fs->remove(fs->ctx, f->marker);
        fs->remove(fs->ctx, f->tmp);
        return false;
    }
    if(!fs->rename(fs->ctx, f->tmp, f->path)) return false; // recover() finishes it
    fs->remove(fs->ctx, f->marker);
    return true;
}
