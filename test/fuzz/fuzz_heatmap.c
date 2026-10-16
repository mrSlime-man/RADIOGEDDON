/**
 * Fuzz target: the RF activity heatmap (helpers/rg_heatmap.c), which parses
 * activity log files from the SD card.
 *
 * Input: [chunk size] then the file's bytes, probed and binned as the app does.
 *
 * Invariants (abort on violation):
 *  - at most RG_HEATMAP_MAX_CH distinct frequencies; the grid's size matches;
 *  - the same map whatever the chunking;
 *  - every cell is in range: a level is one of the five, coverage <= 100,
 *    an active or quiet cell was visited; the span covers the latest row;
 *  - the footer text of every cell is NUL-terminated and fits its buffer.
 */
#include "../../helpers/rg_heatmap.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

static RgHeatCell* map_once(RgHeatmap* h, const char* d, size_t n, size_t chunk) {
    rg_heatmap_probe_begin(h);
    for(size_t off = 0; off < n && !rg_heatmap_probe_done(h); off += chunk)
        rg_heatmap_feed(h, d + off, n - off < chunk ? n - off : chunk);
    rg_heatmap_end(h);
    REQUIRE(h->channels <= RG_HEATMAP_MAX_CH);
    size_t bytes = rg_heatmap_cells_bytes(h);
    REQUIRE(bytes == (size_t)h->channels * RG_HEATMAP_COLS * sizeof(RgHeatCell));
    if(bytes == 0) return NULL;
    RgHeatCell* cells = malloc(bytes);
    rg_heatmap_begin(h, cells);
    for(size_t off = 0; off < n; off += chunk)
        rg_heatmap_feed(h, d + off, n - off < chunk ? n - off : chunk);
    rg_heatmap_end(h);
    return cells;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 1 || size > 65536) return 0;
    size_t chunk = 1 + data[0] % 97;
    const char* d = (const char*)data + 1;
    size_t n = size - 1;
    static RgHeatmap a, b;
    RgHeatCell* ca = map_once(&a, d, n, chunk);
    RgHeatCell* cb = map_once(&b, d, n, n ? n : 1);
    REQUIRE(a.channels == b.channels && a.rows == b.rows && a.span_s == b.span_s);
    if(ca) REQUIRE(memcmp(ca, cb, rg_heatmap_cells_bytes(&a)) == 0);
    if(ca) {
        if(a.rows) REQUIRE(a.span_s > 0 && a.cols_used <= RG_HEATMAP_COLS);
        for(size_t r = 0; r < a.channels; r++) {
            for(size_t c = 0; c < RG_HEATMAP_COLS; c++) {
                const RgHeatCell* cell = rg_heatmap_cell(&a, r, c);
                REQUIRE(cell != NULL);
                for(int m = 0; m < 2; m++) {
                    RgHeatLevel l = rg_heatmap_level(&a, cell, (RgHeatMetric)m);
                    REQUIRE(l <= RgHeatHigh);
                    if(l != RgHeatNotObserved) REQUIRE(cell->flags & RG_HEAT_VISITED);
                }
                REQUIRE(rg_heatmap_coverage(&a, cell) <= 100);
                char text[40];
                size_t len = rg_heatmap_cell_text(&a, r, c, text, sizeof(text));
                REQUIRE(len < sizeof(text) && text[len] == '\0');
                len = rg_heatmap_col_time(&a, c, text, sizeof(text));
                REQUIRE(len < sizeof(text) && text[len] == '\0');
            }
        }
        REQUIRE(rg_heatmap_cell(&a, a.channels, 0) == NULL);
    }
    free(ca);
    free(cb);
    return 0;
}
