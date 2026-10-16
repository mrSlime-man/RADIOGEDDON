#include "radiogeddon_heatmap.h"

#if RG_FEATURE_ACTIVITY_LOG

#include "rg_actlog.h"

#define HEATMAP_CHUNK 512u

/* Feed one file to the map; false if it could not be opened. */
static bool actlog_heatmap_feed(Storage* storage, const char* path, RgHeatmap* map, char* buf) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        size_t n;
        while((n = storage_file_read(file, buf, HEATMAP_CHUNK)) > 0) {
            rg_heatmap_feed(map, buf, n);
            if(map->probing && rg_heatmap_probe_done(map)) break;
        }
        rg_heatmap_end(map);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

/* The log's base path (without ".csv"), from any of its parts:
 * "X_3.csv" is part 3 of "X" when "X.csv" exists. */
static void actlog_heatmap_base(Storage* storage, const char* path, FuriString* base) {
    furi_string_set(base, path);
    size_t n = furi_string_size(base);
    if(n > 4u && strcmp(furi_string_get_cstr(base) + n - 4u, ".csv") == 0)
        furi_string_left(base, n - 4u);
    size_t len = furi_string_size(base);
    const char* s = furi_string_get_cstr(base);
    size_t u = len;
    while(u > 0 && s[u - 1] >= '0' && s[u - 1] <= '9')
        u--;
    if(u > 1 && u < len && s[u - 1] == '_') {
        FuriString* first = furi_string_alloc_set(base);
        furi_string_left(first, u - 1u);
        furi_string_cat_str(first, ".csv");
        if(storage_common_exists(storage, furi_string_get_cstr(first)))
            furi_string_left(base, u - 1u);
        furi_string_free(first);
    }
}

RadioGeddonHeatmapStatus radiogeddon_actlog_heatmap(
    Storage* storage,
    const char* path,
    RgHeatmap* map,
    RgHeatCell** cells,
    size_t spare) {
    *cells = NULL;
    FuriString* base = furi_string_alloc();
    FuriString* part = furi_string_alloc();
    char* buf = malloc(HEATMAP_CHUNK);
    actlog_heatmap_base(storage, path, base);
    RadioGeddonHeatmapStatus status = RadioGeddonHeatmapOk;
    do {
        // 1. The frequencies, from the first interval of the first part.
        rg_heatmap_probe_begin(map);
        furi_string_printf(part, "%s.csv", furi_string_get_cstr(base));
        if(!actlog_heatmap_feed(storage, furi_string_get_cstr(part), map, buf)) {
            status = RadioGeddonHeatmapNoFile;
            break;
        }
        size_t bytes = rg_heatmap_cells_bytes(map);
        if(bytes == 0) {
            status = RadioGeddonHeatmapNoRows;
            break;
        }
        if(memmgr_heap_get_max_free_block() < bytes + spare) {
            status = RadioGeddonHeatmapNoMemory;
            break;
        }
        // 2. Every part, in order, into the grid.
        *cells = malloc(bytes);
        rg_heatmap_begin(map, *cells);
        char name[128];
        for(uint8_t p = 0; p < RG_ACTLOG_MAX_PARTS; p++) {
            // The writer's own part names (rg_actlog_part_name).
            if(!rg_actlog_part_name(furi_string_get_cstr(base), p, name, sizeof(name))) break;
            if(!actlog_heatmap_feed(storage, name, map, buf)) break;
        }
        if(map->rows == 0) status = RadioGeddonHeatmapNoRows;
    } while(false);
    if(status != RadioGeddonHeatmapOk) {
        free(*cells);
        *cells = NULL;
    }
    free(buf);
    furi_string_free(part);
    furi_string_free(base);
    return status;
}


#endif
