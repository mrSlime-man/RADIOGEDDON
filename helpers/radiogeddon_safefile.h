/**
 * @file radiogeddon_safefile.h
 * @brief rg_safefile on the firmware's Storage: crash-safe replacement of app files.
 *
 * Every file the app rewrites in place (settings, journal, favorites, scan
 * profiles, the fingerprint index) is saved as:
 *
 *   if(radiogeddon_safefile_begin(storage, &f)) {
 *       ... write and close f.tmp ...
 *       ok = written ? radiogeddon_safefile_commit(storage, &f)
 *                    : (storage_common_remove(storage, f.tmp), false);
 *   }
 *
 * and loaded after radiogeddon_safefile_recover(storage, &f). See rg_safefile.h.
 */
#pragma once

#include <storage/storage.h>
#include "rg_safefile.h"

RgSafeRecover radiogeddon_safefile_recover(Storage* storage, const RgSafeFile* f);
bool radiogeddon_safefile_begin(Storage* storage, const RgSafeFile* f);
bool radiogeddon_safefile_commit(Storage* storage, const RgSafeFile* f);
