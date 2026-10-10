#include "radiogeddon_scene.h"

#include <datetime/datetime.h>

// File details: what the file system and the Database index know about the
// open file, including its duplicates. Works for damaged files too.

#define FILE_DETAILS_MAX_DUPS  8u
#define FILE_DETAILS_PEEK_SIZE 40u

static void radiogeddon_scene_file_details_time(FuriString* out, uint32_t timestamp) {
    if(timestamp == 0) {
        furi_string_cat_str(out, "unknown");
        return;
    }
    DateTime dt;
    datetime_timestamp_to_datetime(timestamp, &dt);
    furi_string_cat_printf(
        out, "%04u-%02u-%02u %02u:%02u", dt.year, dt.month, dt.day, dt.hour, dt.minute);
}

/* The first line of a damaged file, with anything unprintable shown as '.'. */
static void radiogeddon_scene_file_details_peek(RadioGeddonApp* app, FuriString* out) {
    char buf[FILE_DETAILS_PEEK_SIZE + 1];
    size_t n = 0;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(
           file, furi_string_get_cstr(app->file_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        n = storage_file_read(file, buf, FILE_DETAILS_PEEK_SIZE);
    }
    storage_file_close(file);
    storage_file_free(file);
    if(n == 0) {
        furi_string_cat_str(out, "Contents: none\n");
        return;
    }
    size_t len = 0;
    while(len < n && buf[len] != '\n' && buf[len] != '\r') {
        unsigned char c = (unsigned char)buf[len];
        if(c < 0x20 || c > 0x7E) buf[len] = '.';
        len++;
    }
    buf[len] = '\0';
    furi_string_cat_printf(out, "Starts with:\n%s\n", buf);
}

static void radiogeddon_scene_file_details_dups(RadioGeddonApp* app, const char* file_name) {
    if(!app->db) return;
    const RgDb* db = &app->db->db;
    int32_t self = rg_db_find(db, file_name);
    if(self < 0) return;
    const RgDbEntry* e = &db->entries[self];
    if(!e->dup_count) return;
    furi_string_cat_printf(app->temp_str, "Duplicates: %u\n", (unsigned)e->dup_count);
    size_t shown = 0;
    for(size_t i = 0; i < db->count; i++) {
        if(i == (size_t)self || !rg_db_same_signal(e, &db->entries[i])) continue;
        if(shown == FILE_DETAILS_MAX_DUPS) {
            furi_string_cat_printf(
                app->temp_str, " and %u more\n", (unsigned)(e->dup_count - shown));
            break;
        }
        furi_string_cat_printf(app->temp_str, " %s\n", rg_db_name(db, &db->entries[i]));
        shown++;
    }
}

void radiogeddon_scene_file_details_on_enter(void* context) {
    RadioGeddonApp* app = context;
    FuriString* out = app->temp_str;
    const char* path = furi_string_get_cstr(app->file_path);
    const char* slash = strrchr(path, '/');
    const char* file_name = slash ? slash + 1 : path;

    furi_string_reset(out);
    furi_string_cat_printf(out, "File: %s\n", file_name);
    FileInfo info;
    if(storage_common_stat(app->storage, path, &info) == FSE_OK) {
        furi_string_cat_printf(out, "Size: %lu bytes\n", (unsigned long)info.size);
    }
    uint32_t mtime = 0;
    storage_common_timestamp(app->storage, path, &mtime);
    furi_string_cat_str(out, "Modified: ");
    radiogeddon_scene_file_details_time(out, mtime);
    furi_string_cat_str(out, "\n");

    if(app->file_damaged) {
        furi_string_cat_str(out, "Type: not a readable\nSub-GHz .sub file\n");
        radiogeddon_scene_file_details_peek(app, out);
    } else {
        const RadioGeddonLoadedSignal* sig = &app->loaded;
        if(sig->kind == RadioGeddonSignalKindRaw) {
            furi_string_cat_printf(
                out, "Type: RAW capture\nSamples: %u\n", (unsigned)sig->raw_sample_count);
        } else if(sig->kind == RadioGeddonSignalKindProtocol) {
            furi_string_cat_printf(
                out, "Type: decoded\nProtocol: %s\n", furi_string_get_cstr(sig->protocol));
            if(sig->bit_count)
                furi_string_cat_printf(out, "Bits: %lu\n", (unsigned long)sig->bit_count);
        } else {
            furi_string_cat_str(out, "Type: Sub-GHz file\nwithout a protocol\n");
        }
        if(sig->frequency) {
            furi_string_cat_printf(
                out,
                "Freq: %lu.%03lu MHz\n",
                (unsigned long)(sig->frequency / 1000000),
                (unsigned long)((sig->frequency % 1000000) / 1000));
        }
        if(furi_string_size(sig->preset)) {
            furi_string_cat_printf(out, "Preset: %s\n", furi_string_get_cstr(sig->preset));
        }
    }
    radiogeddon_scene_file_details_dups(app, file_name);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(out));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_file_details_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_file_details_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
}
