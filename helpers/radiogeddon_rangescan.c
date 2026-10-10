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
        furi_mutex_release(instance->mutex);

        if(started && instance->callback) instance->callback(index, instance->context);

        if(hold < 0) {
            if(++next >= count) {
                next = 0;
                uint32_t now = furi_get_tick();
                furi_mutex_acquire(instance->mutex, FuriWaitForever);
                instance->sweep_ms = now - sweep_start;
                if(instance->sweeps < UINT32_MAX) instance->sweeps++;
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

#endif
