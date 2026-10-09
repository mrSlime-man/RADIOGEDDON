#include "radiogeddon_scanner.h"

#define TAG "RadioGeddonScanner"

#define SCANNER_THREAD_STACK  1536
#define SCANNER_PAUSE_POLL_MS 50

struct RadioGeddonScanner {
    RadioGeddonSubGhz* subghz;
    FuriMutex* mutex;
    FuriThread* thread;
    volatile bool stop_requested;

    // Guarded by mutex.
    size_t count;
    uint32_t frequencies[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    RgScanChannel channels[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    RgScanParams params;
    uint16_t dwell_ms;
    uint8_t threshold_db;
    bool hold_on_hit;
    bool paused;
    int32_t hold_index;
    uint32_t sweep_ms;

    RadioGeddonScannerHitCallback callback;
    void* context;
};

RadioGeddonScanner* radiogeddon_scanner_alloc(RadioGeddonSubGhz* subghz) {
    RadioGeddonScanner* instance = malloc(sizeof(RadioGeddonScanner));
    memset(instance, 0, sizeof(RadioGeddonScanner));
    instance->subghz = subghz;
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->hold_index = -1;
    rg_scan_params_default(&instance->params);
    instance->dwell_ms = 10;
    instance->threshold_db = 10;
    return instance;
}

void radiogeddon_scanner_free(RadioGeddonScanner* instance) {
    furi_assert(instance);
    radiogeddon_scanner_stop(instance);
    furi_mutex_free(instance->mutex);
    free(instance);
}

void radiogeddon_scanner_configure(
    RadioGeddonScanner* instance,
    const uint32_t* frequencies,
    size_t count,
    uint16_t dwell_ms,
    uint8_t threshold_db,
    bool hold_on_hit) {
    furi_assert(!instance->thread);
    if(count > RADIOGEDDON_SCANNER_MAX_CHANNELS) count = RADIOGEDDON_SCANNER_MAX_CHANNELS;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    bool same_list = (count == instance->count) &&
                     (memcmp(frequencies, instance->frequencies, count * sizeof(uint32_t)) == 0);
    if(!same_list) {
        instance->count = count;
        memcpy(instance->frequencies, frequencies, count * sizeof(uint32_t));
        for(size_t i = 0; i < RADIOGEDDON_SCANNER_MAX_CHANNELS; i++) {
            rg_scan_channel_reset(&instance->channels[i]);
        }
        instance->sweep_ms = 0;
        instance->hold_index = -1;
    }
    instance->dwell_ms = dwell_ms;
    instance->threshold_db = threshold_db;
    instance->params.threshold_db = (float)threshold_db;
    instance->hold_on_hit = hold_on_hit;
    if(!hold_on_hit) instance->hold_index = -1;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_scanner_set_callback(
    RadioGeddonScanner* instance,
    RadioGeddonScannerHitCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

static int32_t radiogeddon_scanner_thread(void* context) {
    RadioGeddonScanner* instance = context;
    radiogeddon_subghz_scan_begin(instance->subghz);

    size_t next = 0;
    uint32_t sweep_start = furi_get_tick();

    while(!instance->stop_requested) {
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        bool paused = instance->paused;
        size_t count = instance->count;
        int32_t hold = instance->hold_index;
        uint16_t dwell = instance->dwell_ms;
        furi_mutex_release(instance->mutex);

        if(paused || count == 0) {
            furi_delay_ms(SCANNER_PAUSE_POLL_MS);
            continue;
        }

        size_t index = (hold >= 0 && (size_t)hold < count) ? (size_t)hold : next;
        uint32_t freq = instance->frequencies[index];
        float rssi = radiogeddon_subghz_probe_rssi_dwell(instance->subghz, freq, dwell);

        bool started = false;
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        // The list cannot change while running (configure requires stop), so
        // index is still valid here.
        started = rg_scan_channel_update(
            &instance->channels[index], rssi, &instance->params, furi_get_tick());
        if(started && instance->hold_on_hit && instance->hold_index < 0) {
            instance->hold_index = (int32_t)index;
        }
        furi_mutex_release(instance->mutex);

        if(started && instance->callback) instance->callback(index, instance->context);

        if(hold < 0) {
            next++;
            if(next >= count) {
                next = 0;
                uint32_t now = furi_get_tick();
                furi_mutex_acquire(instance->mutex, FuriWaitForever);
                instance->sweep_ms = now - sweep_start;
                furi_mutex_release(instance->mutex);
                sweep_start = now;
            }
        } else {
            // While holding, restart sweep timing when the hold is released.
            sweep_start = furi_get_tick();
        }
    }

    radiogeddon_subghz_scan_end(instance->subghz);
    return 0;
}

void radiogeddon_scanner_start(RadioGeddonScanner* instance) {
    if(instance->thread) return;
    instance->stop_requested = false;
    instance->thread =
        furi_thread_alloc_ex(TAG, SCANNER_THREAD_STACK, radiogeddon_scanner_thread, instance);
    furi_thread_start(instance->thread);
}

void radiogeddon_scanner_stop(RadioGeddonScanner* instance) {
    if(!instance->thread) return;
    instance->stop_requested = true;
    furi_thread_join(instance->thread);
    furi_thread_free(instance->thread);
    instance->thread = NULL;
}

void radiogeddon_scanner_toggle_pause(RadioGeddonScanner* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    if(!instance->paused && instance->hold_index >= 0) {
        // Holding on a hit: the first press resumes the sweep.
        instance->hold_index = -1;
    } else {
        instance->paused = !instance->paused;
        if(!instance->paused) instance->hold_index = -1;
    }
    furi_mutex_release(instance->mutex);
}

void radiogeddon_scanner_release_hold(RadioGeddonScanner* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    instance->hold_index = -1;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_scanner_reset_peaks(RadioGeddonScanner* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    for(size_t i = 0; i < instance->count; i++) {
        rg_scan_channel_reset_peak(&instance->channels[i]);
    }
    furi_mutex_release(instance->mutex);
}

void radiogeddon_scanner_snapshot(RadioGeddonScanner* instance, RadioGeddonScannerSnapshot* out) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    out->count = instance->count;
    memcpy(out->frequencies, instance->frequencies, sizeof(out->frequencies));
    memcpy(out->channels, instance->channels, sizeof(out->channels));
    out->global_floor = rg_scan_global_floor(instance->channels, instance->count);
    out->hold_index = instance->hold_index;
    out->paused = instance->paused;
    out->sweep_ms = instance->sweep_ms;
    out->threshold_db = instance->threshold_db;
    furi_mutex_release(instance->mutex);
}

size_t radiogeddon_scanner_count(RadioGeddonScanner* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    size_t n = instance->count;
    furi_mutex_release(instance->mutex);
    return n;
}

static bool radiogeddon_scanner_write_str(File* file, const char* str) {
    size_t len = strlen(str);
    return storage_file_write(file, str, len) == len;
}

bool radiogeddon_scanner_save_csv(
    RadioGeddonScanner* instance,
    Storage* storage,
    const char* path,
    const char* preset_label) {
    // Snapshot to the heap: it is too large for the GUI thread's stack.
    RadioGeddonScannerSnapshot* snap = malloc(sizeof(RadioGeddonScannerSnapshot));
    radiogeddon_scanner_snapshot(instance, snap);
    uint16_t dwell;
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    dwell = instance->dwell_ms;
    furi_mutex_release(instance->mutex);

    File* file = storage_file_alloc(storage);
    bool ok = false;
    bool opened = false;
    char line[96];
    do {
        if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;
        opened = true;
        if(!radiogeddon_scanner_write_str(
               file,
               "# RadioGeddon narrowband RSSI scan (CC1101, one frequency at a time;\n"
               "# not a wideband spectrum capture).\n"))
            break;
        snprintf(
            line,
            sizeof(line),
            "# Preset: %s, dwell %u ms, threshold +%u dB over noise floor\n",
            preset_label,
            (unsigned)dwell,
            (unsigned)snap->threshold_db);
        if(!radiogeddon_scanner_write_str(file, line)) break;
        if(!radiogeddon_scanner_write_str(file, "Freq_Hz,Last_dBm,Peak_dBm,Floor_dBm,Hits\n"))
            break;
        bool rows_ok = true;
        for(size_t i = 0; i < snap->count && rows_ok; i++) {
            if(snap->channels[i].samples == 0) continue; // never measured
            size_t n = rg_scan_format_row(
                line, sizeof(line) - 1, snap->frequencies[i], &snap->channels[i]);
            if(n == 0) continue;
            line[n] = '\n';
            line[n + 1] = '\0';
            rows_ok = radiogeddon_scanner_write_str(file, line);
        }
        ok = rows_ok;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);
    // A cut-off CSV is worse than none (the screen says the save failed).
    if(opened && !ok) storage_common_remove(storage, path);
    free(snap);
    return ok;
}

int32_t radiogeddon_scanner_hold_index(RadioGeddonScanner* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    int32_t hold = instance->hold_index;
    furi_mutex_release(instance->mutex);
    return hold;
}

uint32_t radiogeddon_scanner_frequency(RadioGeddonScanner* instance, size_t index) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    uint32_t f = (index < instance->count) ? instance->frequencies[index] : 0;
    furi_mutex_release(instance->mutex);
    return f;
}
