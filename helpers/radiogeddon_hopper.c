#include "radiogeddon_hopper.h"
#include "radiogeddon_storage.h"

#include <datetime/datetime.h>
#include <furi_hal_rtc.h>

#define TAG "RadioGeddonHopper"

#define HOPPER_THREAD_STACK 3072
#define HOPPER_SAMPLE_MS    10
#define HOPPER_SETTLE_MS    5 // after a retune, before trusting RSSI
/** Captures shorter than this are treated as noise and not saved. */
#define HOPPER_MIN_RAW_SAVE 64

struct RadioGeddonHopper {
    RadioGeddonSubGhz* subghz;
    FuriMutex* mutex;
    FuriThread* thread;
    volatile bool stop_requested;

    // Guarded by mutex.
    RgHop hop;
    RgHopConfig config;
    uint32_t frequencies[RG_HOP_MAX_CHANNELS];
    bool auto_record;
    bool cmd_toggle_lock;
    bool cmd_next;
    bool pending_activity; // activity started by a decode, for the thread to act on
    float last_rssi;

    // Owned by the hopper thread.
    bool recording;

    RadioGeddonHopperCallback callback;
    void* context;
};

RadioGeddonHopper* radiogeddon_hopper_alloc(RadioGeddonSubGhz* subghz) {
    RadioGeddonHopper* instance = malloc(sizeof(RadioGeddonHopper));
    memset(instance, 0, sizeof(RadioGeddonHopper));
    instance->subghz = subghz;
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    rg_hop_config_default(&instance->config);
    rg_hop_init(&instance->hop, 0, 0);
    instance->last_rssi = RG_SCAN_RSSI_NONE;
    return instance;
}

void radiogeddon_hopper_free(RadioGeddonHopper* instance) {
    furi_assert(instance);
    radiogeddon_hopper_stop(instance);
    furi_mutex_free(instance->mutex);
    free(instance);
}

void radiogeddon_hopper_configure(
    RadioGeddonHopper* instance,
    const uint32_t* frequencies,
    size_t count,
    uint16_t dwell_ms,
    uint16_t hold_ms,
    uint8_t threshold_db,
    bool auto_record) {
    furi_assert(!instance->thread);
    if(count > RG_HOP_MAX_CHANNELS) count = RG_HOP_MAX_CHANNELS;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    bool same_list = (count == instance->hop.count) &&
                     (memcmp(frequencies, instance->frequencies, count * sizeof(uint32_t)) == 0);
    if(!same_list) {
        memset(instance->frequencies, 0, sizeof(instance->frequencies));
        memcpy(instance->frequencies, frequencies, count * sizeof(uint32_t));
        rg_hop_init(&instance->hop, count, furi_get_tick());
    }
    instance->config.dwell_ms = dwell_ms;
    instance->config.hold_ms = hold_ms;
    instance->config.detect.threshold_db = (float)threshold_db;
    instance->auto_record = auto_record;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_hopper_set_callback(
    RadioGeddonHopper* instance,
    RadioGeddonHopperCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

uint32_t radiogeddon_hopper_current_frequency(RadioGeddonHopper* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    uint32_t f = instance->hop.count ? instance->frequencies[instance->hop.current] : 0;
    furi_mutex_release(instance->mutex);
    return f;
}

static void radiogeddon_hopper_emit(RadioGeddonHopper* instance, RadioGeddonHopperEvent event) {
    if(instance->callback) instance->callback(event, instance->context);
}

static void radiogeddon_hopper_record_begin(RadioGeddonHopper* instance) {
    if(instance->recording) return;
    RadioGeddonRecordError err = radiogeddon_subghz_record_start(instance->subghz);
    instance->recording = err == RadioGeddonRecordOk;
    if(!instance->recording) {
        FURI_LOG_W(TAG, "Auto-record skipped: %s", radiogeddon_recorder_error_text(err));
    }
}

static void radiogeddon_hopper_record_finish(RadioGeddonHopper* instance) {
    if(!instance->recording) return;
    instance->recording = false;
    radiogeddon_subghz_record_stop(instance->subghz);
    RadioGeddonRecordStats st;
    radiogeddon_subghz_record_status(instance->subghz, &st);
    if(!radiogeddon_subghz_record_pending(instance->subghz)) {
        // Nothing captured, or the card failed (the file is already gone).
        if(st.error != RadioGeddonRecordOk) {
            radiogeddon_hopper_emit(instance, RadioGeddonHopperEventRecordFailed);
        }
        return;
    }
    if(st.samples < HOPPER_MIN_RAW_SAVE) {
        radiogeddon_subghz_record_discard(instance->subghz); // too short to be useful
        return;
    }

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char name[32];
    snprintf(
        name,
        sizeof(name),
        "HOP_%04u%02u%02u_%02u%02u%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc();
    bool ok = radiogeddon_storage_make_unique_path(storage, path, name) &&
              radiogeddon_subghz_record_save(instance->subghz, furi_string_get_cstr(path));
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    // A successful save moves the file; delete it on failure.
    if(!ok) radiogeddon_subghz_record_discard(instance->subghz);

    if(ok) {
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        rg_hop_mark_recorded(&instance->hop);
        furi_mutex_release(instance->mutex);
    }
    radiogeddon_hopper_emit(
        instance, ok ? RadioGeddonHopperEventRecordSaved : RadioGeddonHopperEventRecordFailed);
}

static void radiogeddon_hopper_retune(RadioGeddonHopper* instance, size_t channel) {
    // A capture must never span two frequencies.
    radiogeddon_hopper_record_finish(instance);
    uint32_t freq = instance->frequencies[channel];
    radiogeddon_subghz_rx_retune(instance->subghz, freq);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    rg_hop_tuned(&instance->hop, channel, furi_get_tick());
    furi_mutex_release(instance->mutex);
    radiogeddon_hopper_emit(instance, RadioGeddonHopperEventRetuned);
    furi_delay_ms(HOPPER_SETTLE_MS);
}

static int32_t radiogeddon_hopper_thread(void* context) {
    RadioGeddonHopper* instance = context;

    while(!instance->stop_requested) {
        // Commands from the GUI.
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        bool do_next = instance->cmd_next;
        instance->cmd_next = false;
        if(instance->cmd_toggle_lock) {
            instance->cmd_toggle_lock = false;
            rg_hop_set_locked(&instance->hop, !instance->hop.locked, furi_get_tick());
        }
        size_t next = rg_hop_next_channel(&instance->hop);
        bool can_hop = instance->hop.count > 1;
        furi_mutex_release(instance->mutex);
        if(do_next && can_hop) radiogeddon_hopper_retune(instance, next);

        float rssi = radiogeddon_subghz_get_rssi(instance->subghz);

        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        instance->last_rssi = rssi;
        RgHopStep step = rg_hop_feed(&instance->hop, &instance->config, rssi, furi_get_tick());
        bool started = step.activity_started || instance->pending_activity;
        instance->pending_activity = false;
        bool auto_record = instance->auto_record;
        furi_mutex_release(instance->mutex);

        if(started) {
            if(auto_record) radiogeddon_hopper_record_begin(instance);
            radiogeddon_hopper_emit(instance, RadioGeddonHopperEventActivity);
        }
        if(step.activity_ended) radiogeddon_hopper_record_finish(instance);
        if(step.retune) radiogeddon_hopper_retune(instance, step.next_channel);

        furi_delay_ms(HOPPER_SAMPLE_MS);
    }

    radiogeddon_hopper_record_finish(instance);
    return 0;
}

void radiogeddon_hopper_start(RadioGeddonHopper* instance) {
    if(instance->thread) return;
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    // Fresh dwell on the current frequency; any hold from a previous visit ends.
    rg_hop_tuned(&instance->hop, instance->hop.current, furi_get_tick());
    instance->cmd_next = false;
    instance->cmd_toggle_lock = false;
    instance->pending_activity = false;
    furi_mutex_release(instance->mutex);

    instance->stop_requested = false;
    instance->thread =
        furi_thread_alloc_ex(TAG, HOPPER_THREAD_STACK, radiogeddon_hopper_thread, instance);
    furi_thread_start(instance->thread);
}

void radiogeddon_hopper_stop(RadioGeddonHopper* instance) {
    if(!instance->thread) return;
    instance->stop_requested = true;
    furi_thread_join(instance->thread);
    furi_thread_free(instance->thread);
    instance->thread = NULL;
}

void radiogeddon_hopper_toggle_lock(RadioGeddonHopper* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    instance->cmd_toggle_lock = true;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_hopper_next(RadioGeddonHopper* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    instance->cmd_next = true;
    furi_mutex_release(instance->mutex);
}

void radiogeddon_hopper_note_decode(RadioGeddonHopper* instance, const char* protocol) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    if(rg_hop_note_decode(&instance->hop, &instance->config, protocol, furi_get_tick())) {
        instance->pending_activity = true;
    }
    furi_mutex_release(instance->mutex);
}

void radiogeddon_hopper_status(RadioGeddonHopper* instance, RadioGeddonHopperStatus* out) {
    uint32_t now = furi_get_tick();
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    const RgHop* hop = &instance->hop;
    out->count = hop->count;
    out->index = hop->current;
    out->frequency = hop->count ? instance->frequencies[hop->current] : 0;
    out->rssi = instance->last_rssi;
    const RgScanChannel* ch = &hop->channels[hop->current];
    out->threshold_dbm = (hop->count && ch->samples >= RG_SCAN_WARMUP_SAMPLES) ?
                             ch->floor + instance->config.detect.threshold_db :
                             RG_SCAN_RSSI_NONE;
    out->locked = hop->locked;
    out->holding = hop->holding;
    out->hold_left_ms = rg_hop_hold_remaining(hop, now);
    out->recording = instance->recording;
    furi_mutex_release(instance->mutex);
    bool active = radiogeddon_subghz_record_status(instance->subghz, &out->record);
    out->recording = out->recording && active;
}

void radiogeddon_hopper_copy_state(RadioGeddonHopper* instance, RgHop* out, uint32_t* frequencies) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    memcpy(out, &instance->hop, sizeof(RgHop));
    memcpy(frequencies, instance->frequencies, sizeof(instance->frequencies));
    furi_mutex_release(instance->mutex);
}

static void radiogeddon_hopper_fmt_mhz(FuriString* out, uint32_t hz) {
    furi_string_cat_printf(
        out, "%lu.%02lu", (unsigned long)(hz / 1000000), (unsigned long)((hz % 1000000) / 10000));
}

static void radiogeddon_hopper_fmt_secs(FuriString* out, uint32_t ms) {
    furi_string_cat_printf(
        out, "%lu.%lus", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
}

void radiogeddon_hopper_report(RadioGeddonHopper* instance, FuriString* out) {
    RgHop* hop = malloc(sizeof(RgHop));
    uint32_t* freqs = malloc(sizeof(uint32_t) * RG_HOP_MAX_CHANNELS);
    radiogeddon_hopper_copy_state(instance, hop, freqs);

    furi_string_reset(out);
    furi_string_cat_printf(out, "Per frequency (this session)\n");
    for(size_t i = 0; i < hop->count; i++) {
        const RgScanChannel* ch = &hop->channels[i];
        const RgHopStats* st = &hop->stats[i];
        radiogeddon_hopper_fmt_mhz(out, freqs[i]);
        if(ch->samples == 0) {
            furi_string_cat_printf(out, ": not visited\n");
            continue;
        }
        furi_string_cat_printf(
            out,
            ": %u hits, %u dec\n  peak %d floor %d act ",
            ch->hits,
            st->decodes,
            (int)(ch->peak - 0.5f),
            (int)(ch->floor - 0.5f));
        radiogeddon_hopper_fmt_secs(out, st->active_ms);
        furi_string_cat_printf(out, "\n");
    }

    furi_string_cat_printf(out, "\nRecent activity (newest first)\n");
    if(hop->event_count == 0) furi_string_cat_printf(out, "None yet.\n");
    for(size_t i = 0; i < hop->event_count; i++) {
        const RgHopEvent* e = rg_hop_event(hop, i);
        if(!e) break;
        radiogeddon_hopper_fmt_mhz(out, freqs[e->channel]);
        furi_string_cat_printf(out, " %ddBm ", (int)(e->peak_dbm - 0.5f));
        if(e->open) {
            furi_string_cat_printf(out, "now");
        } else {
            radiogeddon_hopper_fmt_secs(out, e->duration_ms);
        }
        if(e->protocol[0]) furi_string_cat_printf(out, "\n  %s x%u", e->protocol, e->decodes);
        if(e->recorded) furi_string_cat_printf(out, " [RAW saved]");
        furi_string_cat_printf(out, "\n");
    }
    furi_string_cat_printf(
        out,
        "\nRSSI is measured on one frequency at a time; activity elsewhere while "
        "hopping is not seen.");

    free(freqs);
    free(hop);
}
