#include "radiogeddon_rangescan.h"

#if RG_FEATURE_RANGE_SCAN

#define TAG "RadioGeddonRange"

/* The same small stack as the Scanner: the loop holds a few locals only. */
#define RANGESCAN_THREAD_STACK  1536
#define RANGESCAN_PAUSE_POLL_MS 50

struct RadioGeddonRangeScan {
    RadioGeddonSubGhz* subghz;
    FuriMutex* mutex;
    FuriThread* thread;
    volatile bool stop_requested;

    // Guarded by mutex (the range and dwell never change after alloc).
    RgRange range;
    RgScanParams params;
    uint16_t dwell_ms;
    uint8_t threshold_db;
    bool hold_on_hit;
    bool paused;
    int32_t hold_index;
    uint32_t position;
    uint32_t sweeps;
    uint32_t sweep_ms;
    RgScanChannel* channels; // range.points entries
#if RG_FEATURE_WATERFALL
    RgWaterfall waterfall; // used when waterfall_on (buffer owned by the caller)
    bool waterfall_on;
#endif

    RadioGeddonRangeScanHitCallback callback;
    void* context;
};

size_t radiogeddon_rangescan_memory(uint32_t points) {
    return sizeof(RadioGeddonRangeScan) + (size_t)points * sizeof(RgScanChannel);
}

RadioGeddonRangeScan* radiogeddon_rangescan_alloc(
    RadioGeddonSubGhz* subghz,
    const RgRange* range,
    uint16_t dwell_ms,
    uint8_t threshold_db,
    bool hold_on_hit) {
    furi_check(range->points > 0 && range->points <= RG_RANGE_MAX_POINTS);
    RadioGeddonRangeScan* instance = malloc(sizeof(RadioGeddonRangeScan));
    memset(instance, 0, sizeof(RadioGeddonRangeScan));
    instance->subghz = subghz;
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->range = *range;
    rg_scan_params_default(&instance->params);
    instance->params.threshold_db = (float)threshold_db;
    instance->dwell_ms = dwell_ms;
    instance->threshold_db = threshold_db;
    instance->hold_on_hit = hold_on_hit;
    instance->hold_index = -1;
    instance->channels = malloc(range->points * sizeof(RgScanChannel));
    for(uint32_t i = 0; i < range->points; i++) {
        rg_scan_channel_reset(&instance->channels[i]);
    }
    return instance;
}

void radiogeddon_rangescan_free(RadioGeddonRangeScan* instance) {
    furi_assert(instance);
    radiogeddon_rangescan_stop(instance);
    free(instance->channels);
    furi_mutex_free(instance->mutex);
    free(instance);
}

void radiogeddon_rangescan_set_callback(
    RadioGeddonRangeScan* instance,
    RadioGeddonRangeScanHitCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

static int32_t radiogeddon_rangescan_thread(void* context) {
    RadioGeddonRangeScan* instance = context;
    radiogeddon_subghz_scan_begin(instance->subghz);

    const uint32_t count = instance->range.points;
    uint32_t next = 0;
    uint32_t sweep_start = furi_get_tick();

    while(!instance->stop_requested) {
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        bool paused = instance->paused;
        int32_t hold = instance->hold_index;
        furi_mutex_release(instance->mutex);

        if(paused) {
            furi_delay_ms(RANGESCAN_PAUSE_POLL_MS);
            sweep_start = furi_get_tick();
            continue;
        }

        uint32_t index = (hold >= 0 && (uint32_t)hold < count) ? (uint32_t)hold : next;
        // Planned points are always inside the bands the radio accepted.
        uint32_t freq = rg_range_frequency(&instance->range, index);
        float rssi =
            radiogeddon_subghz_probe_rssi_dwell(instance->subghz, freq, instance->dwell_ms);

        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        instance->position = index;
        bool started = rg_scan_channel_update(
            &instance->channels[index], rssi, &instance->params, furi_get_tick());
        if(started && instance->hold_on_hit && instance->hold_index < 0) {
            instance->hold_index = (int32_t)index;
        }
#if RG_FEATURE_WATERFALL
        if(instance->waterfall_on) rg_waterfall_add(&instance->waterfall, index, rssi);
#endif
        furi_mutex_release(instance->mutex);

        if(started && instance->callback) instance->callback(index, instance->context);

        if(hold < 0) {
            if(++next >= count) {
                next = 0;
                uint32_t now = furi_get_tick();
                furi_mutex_acquire(instance->mutex, FuriWaitForever);
                instance->sweep_ms = now - sweep_start;
                if(instance->sweeps < UINT32_MAX) instance->sweeps++;
#if RG_FEATURE_WATERFALL
                if(instance->waterfall_on) rg_waterfall_commit(&instance->waterfall, now);
#endif
                furi_mutex_release(instance->mutex);
                sweep_start = now;
            }
        } else {
            sweep_start = furi_get_tick();
        }
    }

    radiogeddon_subghz_scan_end(instance->subghz);
    return 0;
}

void radiogeddon_rangescan_start(RadioGeddonRangeScan* instance) {
    if(instance->thread) return;
    instance->stop_requested = false;
    instance->thread =
        furi_thread_alloc_ex(TAG, RANGESCAN_THREAD_STACK, radiogeddon_rangescan_thread, instance);
    furi_thread_start(instance->thread);
}

void radiogeddon_rangescan_stop(RadioGeddonRangeScan* instance) {
    if(!instance->thread) return;
    instance->stop_requested = true;
    furi_thread_join(instance->thread);
    furi_thread_free(instance->thread);
    instance->thread = NULL;
}

void radiogeddon_rangescan_toggle_pause(RadioGeddonRangeScan* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    if(!instance->paused && instance->hold_index >= 0) {
        instance->hold_index = -1; // holding on a hit: the first press resumes
    } else {
        instance->paused = !instance->paused;
        if(!instance->paused) instance->hold_index = -1;
    }
    furi_mutex_release(instance->mutex);
}

void radiogeddon_rangescan_reset_peaks(RadioGeddonRangeScan* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    for(uint32_t i = 0; i < instance->range.points; i++) {
        rg_scan_channel_reset_peak(&instance->channels[i]);
    }
    furi_mutex_release(instance->mutex);
}

void radiogeddon_rangescan_recalibrate(RadioGeddonRangeScan* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    for(uint32_t i = 0; i < instance->range.points; i++) {
        rg_scan_channel_reset(&instance->channels[i]);
    }
    instance->hold_index = -1;
    instance->sweeps = 0;
    instance->sweep_ms = 0;
#if RG_FEATURE_WATERFALL
    // Readings before the recalibration must not share a row with those after.
    if(instance->waterfall_on) rg_waterfall_discard(&instance->waterfall);
#endif
    furi_mutex_release(instance->mutex);
}

void radiogeddon_rangescan_snapshot(RadioGeddonRangeScan* instance, RadioGeddonRangeSnapshot* out) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    out->range = instance->range;
    out->count = instance->range.points;
    bool calibrating = false;
    for(uint32_t i = 0; i < out->count; i++) {
        rg_spectrum_from_channel(&out->point[i], &instance->channels[i]);
        if(!(out->point[i].flags & RgSpectrumWarm)) calibrating = true;
    }
    out->hold_index = instance->hold_index;
    out->position = instance->position;
    out->paused = instance->paused;
    out->sweeps = instance->sweeps;
    out->sweep_ms = instance->sweep_ms;
    out->dwell_ms = instance->dwell_ms;
    out->threshold_db = instance->threshold_db;
    furi_mutex_release(instance->mutex);
    out->calibrating = calibrating;
    out->floor = rg_spectrum_median_floor(out->point, out->count);
}

uint32_t radiogeddon_rangescan_frequency(RadioGeddonRangeScan* instance, uint32_t index) {
    // The range never changes after alloc: no lock needed.
    return index < instance->range.points ? rg_range_frequency(&instance->range, index) : 0;
}

static bool radiogeddon_rangescan_write_str(File* file, const char* str) {
    size_t len = strlen(str);
    return storage_file_write(file, str, len) == len;
}

bool radiogeddon_rangescan_save_csv(
    RadioGeddonRangeScan* instance,
    Storage* storage,
    const char* path,
    const char* preset_label) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    bool opened = false;
    char line[112];
    do {
        if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;
        opened = true;
        if(!radiogeddon_rangescan_write_str(
               file,
               "# RadioGeddon range scan: narrowband RSSI, one frequency at a time\n"
               "# (CC1101); not a wideband spectrum capture. Gaps the radio cannot\n"
               "# tune are skipped.\n"))
            break;
        snprintf(
            line,
            sizeof(line),
            "# Range %lu-%lu Hz, step %lu Hz, %lu points; preset %s\n",
            (unsigned long)instance->range.start_hz,
            (unsigned long)instance->range.end_hz,
            (unsigned long)instance->range.step_hz,
            (unsigned long)instance->range.points,
            preset_label);
        if(!radiogeddon_rangescan_write_str(file, line)) break;
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        uint32_t sweeps = instance->sweeps;
        uint32_t sweep_ms = instance->sweep_ms;
        furi_mutex_release(instance->mutex);
        snprintf(
            line,
            sizeof(line),
            "# Dwell %u ms, threshold +%u dB, %lu sweeps, last sweep %lu ms\n",
            (unsigned)instance->dwell_ms,
            (unsigned)instance->threshold_db,
            (unsigned long)sweeps,
            (unsigned long)sweep_ms);
        if(!radiogeddon_rangescan_write_str(file, line)) break;
        if(!radiogeddon_rangescan_write_str(file, "Freq_Hz,Last_dBm,Peak_dBm,Floor_dBm,Hits\n"))
            break;
        bool rows_ok = true;
        for(uint32_t i = 0; i < instance->range.points && rows_ok; i++) {
            // Copy one channel under the lock; format and write without it.
            RgScanChannel ch;
            furi_mutex_acquire(instance->mutex, FuriWaitForever);
            ch = instance->channels[i];
            furi_mutex_release(instance->mutex);
            if(ch.samples == 0) continue;
            size_t n = rg_scan_format_row(
                line, sizeof(line) - 1, rg_range_frequency(&instance->range, i), &ch);
            if(n == 0) continue;
            line[n] = '\n';
            line[n + 1] = '\0';
            rows_ok = radiogeddon_rangescan_write_str(file, line);
        }
        ok = rows_ok;
    } while(false);
    storage_file_close(file);
    storage_file_free(file);
    if(opened && !ok) storage_common_remove(storage, path);
    return ok;
}

#if RG_FEATURE_WATERFALL

bool radiogeddon_rangescan_waterfall_attach(
    RadioGeddonRangeScan* instance,
    void* buf,
    size_t bytes) {
    furi_check(!instance->thread);
    instance->waterfall_on =
        rg_waterfall_init(&instance->waterfall, buf, bytes, instance->range.points);
    return instance->waterfall_on;
}

/* Round a float dBm to an int8 floor (RG_WF_NO_FLOOR stays reserved). */
static int8_t radiogeddon_rangescan_floor8(float dbm) {
    float r = dbm + (dbm < 0 ? -0.5f : 0.5f);
    if(r <= -127.0f) return -127;
    if(r >= 127.0f) return 127;
    return (int8_t)r;
}

/* Point of @p column whose peak hold is highest (measured data), else its
 * middle point. Called with the lock held. */
static uint32_t
    radiogeddon_rangescan_column_point(RadioGeddonRangeScan* instance, uint16_t column) {
    uint32_t first, last;
    rg_waterfall_points_of(&instance->waterfall, column, &first, &last);
    uint32_t best = (first + last) / 2u;
    float best_peak = RG_SCAN_RSSI_NONE;
    for(uint32_t i = first; i <= last && i < instance->range.points; i++) {
        const RgScanChannel* ch = &instance->channels[i];
        if(ch->samples && ch->peak > best_peak) {
            best_peak = ch->peak;
            best = i;
        }
    }
    return best;
}

void radiogeddon_rangescan_waterfall_frame(
    RadioGeddonRangeScan* instance,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out) {
    int8_t floors[RG_WF_MAX_COLUMNS];
    memset(out->seg_start, 0, sizeof(out->seg_start));

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    RgWaterfall* wf = &instance->waterfall;
    out->points = instance->range.points;
    out->paused = instance->paused;
    out->sweeps = instance->sweeps;
    out->sweep_ms = instance->sweep_ms;
    out->threshold_db = instance->threshold_db;
    out->estimate_ms = rg_range_sweep_ms(instance->range.points, instance->dwell_ms);
    out->calibrating = false;

    // Per-column floors: the lowest trusted floor of the column's points.
    for(uint16_t c = 0; c < RG_WF_MAX_COLUMNS; c++)
        floors[c] = (int8_t)RG_WF_NO_FLOOR;
    for(uint32_t i = 0; i < instance->range.points; i++) {
        const RgScanChannel* ch = &instance->channels[i];
        if(ch->samples < RG_SCAN_WARMUP_SAMPLES) {
            out->calibrating = true;
            continue;
        }
        uint16_t c = rg_waterfall_column_of(wf, i);
        int8_t f = radiogeddon_rangescan_floor8(ch->floor);
        if(floors[c] == RG_WF_NO_FLOOR || f < floors[c]) floors[c] = f;
    }
    float global = rg_scan_global_floor(instance->channels, instance->range.points);
    out->floor = (int8_t)RG_WF_NO_FLOOR;
    if(global > RG_SCAN_RSSI_NONE) out->floor = radiogeddon_rangescan_floor8(global);

    if(!instance->waterfall_on || wf->columns == 0) {
        furi_mutex_release(instance->mutex);
        memset(out->xbm, 0, sizeof(out->xbm));
        out->drawn = out->filled = out->rows = out->scroll = out->max_scroll = 0;
        out->columns = 0;
        out->cursor = out->cursor_x = out->cursor_w = 0;
        out->cursor_hz = 0;
        out->cursor_dbm = out->cursor_peak = RG_WF_NO_DATA;
        out->have_peak = false;
        out->top_age_ms = 0;
        out->stored = 0;
        return;
    }

    out->columns = wf->columns;
    out->stored = wf->sweeps;
    out->rows = wf->rows;
    out->filled = wf->filled;
    out->max_scroll = rg_waterfall_max_scroll(wf, RADIOGEDDON_WF_VIEW_H);
    out->scroll = request->scroll > out->max_scroll ? out->max_scroll : request->scroll;
    out->cursor = request->cursor >= wf->columns ? (uint16_t)(wf->columns - 1u) : request->cursor;

    RgWaterfallStyle style = {
        .span_db = request->span_db,
        .strong_db = instance->threshold_db,
        .noise_comp = request->noise_comp,
        .floors = floors,
        .floor_all = out->floor,
    };
    // Strong signals are drawn solid: at least the threshold above the
    // reference, and never inside the dither span.
    if(style.strong_db <= style.span_db) style.strong_db = (uint8_t)(style.span_db + 1u);
    out->drawn = rg_waterfall_render(
        wf, &style, out->scroll, out->xbm, RADIOGEDDON_WF_VIEW_W, RADIOGEDDON_WF_VIEW_H);

    // Band segments after the first start where the radio's gaps were skipped.
    uint32_t first_point = 0;
    for(size_t k = 0; k < instance->range.segments; k++) {
        if(k > 0) {
            uint16_t x, w;
            rg_waterfall_column_span(
                wf, rg_waterfall_column_of(wf, first_point), RADIOGEDDON_WF_VIEW_W, &x, &w);
            out->seg_start[x / 8u] |= (uint8_t)(1u << (x % 8u));
        }
        first_point += instance->range.segment[k].count;
    }

    rg_waterfall_column_span(
        wf, out->cursor, RADIOGEDDON_WF_VIEW_W, &out->cursor_x, &out->cursor_w);
    out->cursor_hz = rg_range_frequency(
        &instance->range, radiogeddon_rangescan_column_point(instance, out->cursor));
    out->cursor_dbm = rg_waterfall_cell(wf, out->scroll, out->cursor);
    out->cursor_peak = RG_WF_NO_DATA;
    for(uint16_t a = 0; a < wf->filled; a++) {
        int16_t v = rg_waterfall_cell(wf, a, out->cursor);
        if(v != RG_WF_NO_DATA && (out->cursor_peak == RG_WF_NO_DATA || v > out->cursor_peak))
            out->cursor_peak = v;
    }
    out->have_peak = rg_waterfall_peak(wf, &out->peak_column, &out->peak_age, &out->peak_dbm);
    out->peak_hz =
        out->have_peak ?
            rg_range_frequency(
                &instance->range, radiogeddon_rangescan_column_point(instance, out->peak_column)) :
            0;
    uint32_t newest = rg_waterfall_stamp(wf, 0);
    uint32_t top = rg_waterfall_stamp(wf, out->scroll);
    out->top_age_ms = (wf->filled && newest >= top) ? newest - top : 0;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_rangescan_waterfall_frame_fn(
    void* engine,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out) {
    radiogeddon_rangescan_waterfall_frame(engine, request, out);
}

bool radiogeddon_rangescan_waterfall_save_csv(
    RadioGeddonRangeScan* instance,
    Storage* storage,
    const char* path,
    const char* preset_label) {
    if(!instance->waterfall_on) return false;
    File* file = storage_file_alloc(storage);
    bool ok = false;
    bool opened = false;
    char line[112];
    RgWaterfall* wf = &instance->waterfall;
    do {
        if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;
        opened = true;
        if(!radiogeddon_rangescan_write_str(
               file,
               "# RadioGeddon waterfall: RSSI sweep history. Each row is one\n"
               "# sequential narrowband sweep (CC1101), newest first; each column\n"
               "# is the strongest reading of its points in that sweep. Empty\n"
               "# cells were not measured. Not a wideband or IQ capture.\n"))
            break;
        snprintf(
            line,
            sizeof(line),
            "# Range %lu-%lu Hz, step %lu Hz, %lu points in %u columns; preset %s\n",
            (unsigned long)instance->range.start_hz,
            (unsigned long)instance->range.end_hz,
            (unsigned long)instance->range.step_hz,
            (unsigned long)instance->range.points,
            (unsigned)wf->columns,
            preset_label);
        if(!radiogeddon_rangescan_write_str(file, line)) break;
        snprintf(
            line,
            sizeof(line),
            "# Dwell %u ms; column header = first point's frequency in Hz\n",
            (unsigned)instance->dwell_ms);
        if(!radiogeddon_rangescan_write_str(file, line)) break;
        bool rows_ok = radiogeddon_rangescan_write_str(file, "Age_ms");
        for(uint16_t c = 0; c < wf->columns && rows_ok; c++) {
            uint32_t first, last;
            rg_waterfall_points_of(wf, c, &first, &last);
            snprintf(
                line,
                sizeof(line),
                ",%lu",
                (unsigned long)rg_range_frequency(&instance->range, first));
            rows_ok = radiogeddon_rangescan_write_str(file, line);
        }
        if(rows_ok) rows_ok = radiogeddon_rangescan_write_str(file, "\n");

        // One row copied under the lock at a time; formatted and written
        // without it. Rows that scroll out meanwhile are simply not written.
        int16_t cells[RG_WF_MAX_COLUMNS];
        for(uint16_t age = 0; rows_ok; age++) {
            furi_mutex_acquire(instance->mutex, FuriWaitForever);
            bool have = age < wf->filled;
            uint32_t newest = rg_waterfall_stamp(wf, 0);
            uint32_t stamp = rg_waterfall_stamp(wf, age);
            uint16_t columns = wf->columns;
            for(uint16_t c = 0; have && c < columns; c++)
                cells[c] = rg_waterfall_cell(wf, age, c);
            furi_mutex_release(instance->mutex);
            if(!have) break;
            snprintf(line, sizeof(line), "%lu", (unsigned long)(newest - stamp));
            rows_ok = radiogeddon_rangescan_write_str(file, line);
            for(uint16_t c = 0; c < columns && rows_ok; c++) {
                if(cells[c] == RG_WF_NO_DATA) {
                    rows_ok = radiogeddon_rangescan_write_str(file, ",");
                } else {
                    snprintf(line, sizeof(line), ",%d", (int)cells[c]);
                    rows_ok = radiogeddon_rangescan_write_str(file, line);
                }
            }
            if(rows_ok) rows_ok = radiogeddon_rangescan_write_str(file, "\n");
        }
        ok = rows_ok;
    } while(false);
    storage_file_close(file);
    storage_file_free(file);
    if(opened && !ok) storage_common_remove(storage, path);
    return ok;
}

#endif /* RG_FEATURE_WATERFALL */

#endif
