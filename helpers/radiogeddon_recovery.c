#include "radiogeddon_recovery.h"
#include "radiogeddon_storage.h"
#include "rg_div.h"

#include <datetime/datetime.h>
#include <dialogs/dialogs.h>
#include <furi_hal_rtc.h>

#define TAG "RadioGeddonRecovery"

/* Read size for the scan (a few MB take a few hundred calls). */
#define RECOVERY_CHUNK 1024u

bool radiogeddon_recovery_scan(Storage* storage, RadioGeddonRecovery* out) {
    memset(out, 0, sizeof(*out));
    rg_recover_init(&out->scan);
    FileInfo info;
    if(storage_common_stat(storage, RADIOGEDDON_RECORD_TEMP, &info) != FSE_OK) return false;
    out->size = (uint32_t)(info.size > UINT32_MAX ? UINT32_MAX : info.size);
    if(storage_common_timestamp(storage, RADIOGEDDON_RECORD_TEMP, &out->timestamp) != FSE_OK)
        out->timestamp = 0;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, RADIOGEDDON_RECORD_TEMP, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t* buf = malloc(RECOVERY_CHUNK);
        size_t n;
        while(!out->scan.stopped && (n = storage_file_read(file, buf, RECOVERY_CHUNK)) > 0)
            rg_recover_feed(&out->scan, buf, n);
        free(buf);
    }
    storage_file_close(file);
    storage_file_free(file);
    out->verdict = rg_recover_finish(&out->scan);
    FURI_LOG_I(
        TAG,
        "leftover capture: %lu B, verdict %d, %lu samples, keep %lu B",
        (unsigned long)out->size,
        (int)out->verdict,
        (unsigned long)out->scan.samples,
        (unsigned long)out->scan.cut);
    return true;
}

bool radiogeddon_recovery_repair(Storage* storage, const RadioGeddonRecovery* rec) {
    if(rec->verdict == RgRecoverComplete) return true;
    if(rec->verdict != RgRecoverPartial) return false;
    char tail[128];
    size_t tlen = rg_recover_tail(&rec->scan, tail, sizeof(tail));
    File* file = storage_file_alloc(storage);
    bool ok =
        tlen > 0 &&
        storage_file_open(file, RADIOGEDDON_RECORD_TEMP, FSAM_READ_WRITE, FSOM_OPEN_EXISTING) &&
        storage_file_seek(file, rec->scan.cut, true) && storage_file_truncate(file) &&
        storage_file_write(file, tail, tlen) == tlen;
    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    return ok;
}

bool radiogeddon_recovery_keep(Storage* storage, const RadioGeddonRecovery* rec, FuriString* path) {
    DateTime dt;
    if(rec->timestamp) {
        datetime_timestamp_to_datetime(rec->timestamp, &dt);
    } else {
        furi_hal_rtc_get_datetime(&dt);
    }
    char name[40];
    snprintf(
        name,
        sizeof(name),
        "Recovered_%04u%02u%02u_%02u%02u%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    radiogeddon_storage_ensure_paths(storage);
    if(!radiogeddon_storage_make_unique_path(storage, path, name)) return false;
    return storage_common_rename(storage, RADIOGEDDON_RECORD_TEMP, furi_string_get_cstr(path)) ==
           FSE_OK;
}

void radiogeddon_recovery_discard(Storage* storage) {
    storage_common_remove(storage, RADIOGEDDON_RECORD_TEMP);
}

/* Repaired, then offered back: Back keeps it; only Delete removes it. */
void radiogeddon_recovery_offer(Storage* storage, DialogsApp* dialogs) {
    RadioGeddonRecovery* rec = malloc(sizeof(RadioGeddonRecovery));
    if(!radiogeddon_recovery_scan(storage, rec)) {
        free(rec);
        return;
    }
    if(rec->verdict == RgRecoverNothing || rec->verdict == RgRecoverEmpty) {
        // A header at most: nothing of a capture to give back.
        radiogeddon_recovery_discard(storage);
        free(rec);
        return;
    }
    bool repaired = radiogeddon_recovery_repair(storage, rec);
    uint32_t tenths = rg_div_u64_u32(rec->scan.duration_us, 100000u);
    char text[112];
    snprintf(
        text,
        sizeof(text),
        "Cut off by a restart:\n%lu samples, %lu.%lu s\n%lu.%02lu MHz%s",
        (unsigned long)rec->scan.samples,
        (unsigned long)(tenths / 10u),
        (unsigned long)(tenths % 10u),
        (unsigned long)(rec->scan.frequency / 1000000u),
        (unsigned long)(rec->scan.frequency % 1000000u / 10000u),
        repaired ? "" : "\nEnd may be damaged");
    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, "Unsaved recording", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(msg, text, 64, 34, AlignCenter, AlignCenter);
    dialog_message_set_buttons(msg, "Delete", NULL, "Keep");
    DialogMessageButton answer = dialog_message_show(dialogs, msg);
    if(answer == DialogMessageButtonLeft) {
        radiogeddon_recovery_discard(storage);
    } else {
        FuriString* path = furi_string_alloc();
        bool kept = radiogeddon_recovery_keep(storage, rec, path);
        dialog_message_set_buttons(msg, NULL, "OK", NULL);
        if(kept) {
            const char* slash = strrchr(furi_string_get_cstr(path), '/');
            snprintf(text, sizeof(text), "Saved as\n%s", slash ? slash + 1 : "");
        } else {
            snprintf(
                text,
                sizeof(text),
                "Could not save it now.\nIt is offered again\nat the next start.");
        }
        dialog_message_set_text(msg, text, 64, 34, AlignCenter, AlignCenter);
        dialog_message_show(dialogs, msg);
        furi_string_free(path);
    }
    dialog_message_free(msg);
    free(rec);
}
