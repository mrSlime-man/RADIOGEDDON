/**
 * @file radiogeddon_recovery.h
 * @brief Recording recovery: a capture cut off by a reboot is offered back, not deleted.
 *
 * A RAW capture streams to RADIOGEDDON_RECORD_TEMP until it is saved. If the
 * Flipper restarts first (battery, crash, a reset during recording or before
 * Save), the file is still there at the next start. Earlier versions deleted
 * it; now the app checks it (rg_recover.h), repairs a cut-off end in place
 * (truncate after the last complete value, close the line, add a
 * "# Recovered:" note) and asks whether to keep it. Kept recordings get a
 * name in the signals folder and are ordinary .sub files from then on.
 *
 * Nothing is presented as more than it is: a file with no RAW header or no
 * readable sample is removed (there is nothing of a capture in it), and a
 * repaired file says in its note that its end may be missing.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include "rg_recover.h"

typedef struct {
    RgRecoverScan scan;
    RgRecoverVerdict verdict;
    uint32_t timestamp; /* the file's modification time (0 unknown) */
    uint32_t size; /* bytes on the card */
} RadioGeddonRecovery;

/** Check the leftover capture. False when there is none. */
bool radiogeddon_recovery_scan(Storage* storage, RadioGeddonRecovery* out);

/** Repair a Partial capture in place (complete ones are left as they are). */
bool radiogeddon_recovery_repair(Storage* storage, const RadioGeddonRecovery* rec);

/**
 * Move the capture into the signals folder under a new name
 * ("Recovered_YYYYMMDD_HHMMSS", from its modification time). @p path receives
 * where it went. False if no free name or the move failed (the capture then
 * stays where it is, to be offered again at the next start).
 */
bool radiogeddon_recovery_keep(Storage* storage, const RadioGeddonRecovery* rec, FuriString* path);

/** Delete the capture (the user chose to, or it holds nothing). */
void radiogeddon_recovery_discard(Storage* storage);

/**
 * The whole startup flow: scan the leftover capture, remove it if it holds
 * nothing, else repair it and ask (Keep / Delete; Back keeps), then say where
 * it was saved. Blocks on the firmware's dialog.
 */
void radiogeddon_recovery_offer(Storage* storage, DialogsApp* dialogs);
