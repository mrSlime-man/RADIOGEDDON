#include "radiogeddon_subghz.h"
#include "radiogeddon_storage.h"
#include "rg_memstat.h"
#include "radiogeddon_decode_text.h"

#include <furi_hal_subghz.h>
#include <furi_hal_region.h>
#include <furi_hal_power.h>
#include <power/power_service/power.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>

#define TAG "RadioGeddonSubGhz"

typedef enum {
    RadioGeddonTxModeNone,
    RadioGeddonTxModeProtocol,
    RadioGeddonTxModeRaw,
} RadioGeddonTxMode;

struct RadioGeddonSubGhz {
    const SubGhzDevice* device;
    bool device_begun;
    RadioGeddonRadio radio;
    bool ext_power_ours; // we switched the 5 V pin on for the external module

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    void* raw_decoder; // SubGhzProtocolDecoderRAW* (unused by our own capture)

    uint32_t frequency;
    uint8_t preset_index;

    bool rx_running;
    uint32_t session_cost; // heap the last receive session took to set up
    RadioGeddonSubGhzDecodeCallback decode_cb;
    void* decode_ctx;

    // Streaming RAW capture. `recorder` is read by the worker thread; it is
    // only changed under rec_mutex and freed after the rec_busy handshake.
    RadioGeddonRecorder* recorder;
    bool rec_busy; // the worker thread is inside a push
    FuriMutex* rec_mutex; // recorder lifecycle vs. status readers
    RadioGeddonRecordStats rec_last; // last capture's stats once detached
    bool rec_pending; // a stopped capture waits in the temporary file

    // TX
    RadioGeddonTxMode tx_mode;
    SubGhzTransmitter* transmitter;
    SubGhzFileEncoderWorker* file_encoder;
    RadioGeddonTxCompleteCallback tx_complete_cb;
    void* tx_complete_ctx;
};

static FuriHalSubGhzPreset radiogeddon_subghz_current_preset(RadioGeddonSubGhz* instance) {
    return radiogeddon_presets[instance->preset_index].preset;
}

static SubGhzRadioPreset radiogeddon_subghz_build_radio_preset(RadioGeddonSubGhz* instance) {
    SubGhzRadioPreset preset = {0};
    preset.frequency = instance->frequency;
    preset.name = furi_string_alloc_set(radiogeddon_presets[instance->preset_index].file_name);
    preset.data = NULL;
    preset.data_size = 0;
    return preset;
}

/* ---- Worker / decode plumbing ------------------------------------------ */

/*
 * Hand one event to the recorder, if one is attached. Runs on the worker
 * thread and never blocks: the recorder only appends to a lock-free ring.
 * rec_busy brackets the access so that a stopping thread, after clearing
 * `recorder`, can wait until no call still holds the old pointer (both sides
 * use sequentially consistent order: store own flag, then load the other's).
 */
static void radiogeddon_subghz_record_event(
    RadioGeddonSubGhz* instance,
    bool overrun,
    bool level,
    uint32_t duration) {
    if(!__atomic_load_n(&instance->recorder, __ATOMIC_RELAXED)) return;
    __atomic_store_n(&instance->rec_busy, true, __ATOMIC_SEQ_CST);
    RadioGeddonRecorder* rec = __atomic_load_n(&instance->recorder, __ATOMIC_SEQ_CST);
    if(rec) {
        if(overrun) {
            radiogeddon_recorder_note_overrun(rec);
        } else {
            radiogeddon_recorder_push(rec, level, duration);
        }
    }
    __atomic_store_n(&instance->rec_busy, false, __ATOMIC_SEQ_CST);
}

// Worker reports a capture buffer overrun: drop partial decode state.
static void radiogeddon_subghz_overrun_callback(void* context) {
    RadioGeddonSubGhz* instance = context;
    if(instance->receiver) subghz_receiver_reset(instance->receiver);
    radiogeddon_subghz_record_event(instance, true, false, 0);
}

// Called from the worker thread for every (level,duration) pair received.
static void radiogeddon_subghz_pair_callback(void* context, bool level, uint32_t duration) {
    RadioGeddonSubGhz* instance = context;

    // Feed the protocol decoders (live known-protocol identification).
    if(instance->receiver) {
        subghz_receiver_decode(instance->receiver, level, duration);
    }

    // Stream to the RAW recorder if one is attached (never touches the SD card).
    radiogeddon_subghz_record_event(instance, false, level, duration);
}

// Called by the receiver when a protocol is fully decoded.
static void radiogeddon_subghz_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    RadioGeddonSubGhz* instance = context;

    FuriString* text = furi_string_alloc();
    FuriString* serialized = furi_string_alloc();

    uint8_t hash = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    SubGhzRadioPreset preset = radiogeddon_subghz_build_radio_preset(instance);
    // Not the decoder's own text for the two that would read a rainbow table.
    bool have_text = radiogeddon_decode_text(decoder_base, &preset, text);

    // Produce a complete, loadable .sub representation in RAM.
    FlipperFormat* ff = flipper_format_string_alloc();
    bool have_sub = false;
    do {
        if(!flipper_format_write_header_cstr(
               ff, RADIOGEDDON_SUB_FILE_TYPE, RADIOGEDDON_SUB_FILE_VERSION))
            break;
        if(subghz_protocol_decoder_base_serialize(decoder_base, ff, &preset) !=
           SubGhzProtocolStatusOk)
            break;
        Stream* stream = flipper_format_get_raw_stream(ff);
        stream_rewind(stream);
        size_t size = stream_size(stream);
        uint8_t buf[65];
        size_t read;
        while((read = stream_read(stream, buf, sizeof(buf) - 1)) > 0) {
            buf[read] = '\0';
            furi_string_cat_printf(serialized, "%s", (char*)buf);
        }
        have_sub = (size > 0);
    } while(false);
    flipper_format_free(ff);
    furi_string_free(preset.name);

    const char* name = (decoder_base->protocol && decoder_base->protocol->name) ?
                           decoder_base->protocol->name :
                           "Unknown";

    if(instance->decode_cb && (have_text || have_sub)) {
        instance->decode_cb(name, hash, text, have_sub ? serialized : NULL, instance->decode_ctx);
    }

    furi_string_free(text);
    furi_string_free(serialized);

    subghz_receiver_reset(receiver);
}

/* ---- Lifecycle --------------------------------------------------------- */

// The protocol environment (keystore + registry) and the receiver (one decoder
// instance per registered protocol) are by far the largest allocations this
// toolkit makes; on firmwares with big protocol sets and keystores they alone
// can exhaust the heap. They are therefore only alive while a receive or
// transmit session actually needs them, never at application start.
static void radiogeddon_subghz_environment_acquire(RadioGeddonSubGhz* instance, bool keystore) {
    if(instance->environment) return;
    instance->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)&subghz_protocol_registry);
    // Best-effort keystore load improves KeeLoq manufacturer identification.
    // Only needed for decoding; transmitting static protocols never uses it.
    if(keystore) {
        subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    }
    // No rainbow tables: they undo rolling-code obfuscation, which this toolkit
    // does not do. Official's CAME Atomo and Alutech AT-4N decoders cannot
    // describe a decode without one (radiogeddon_decode_text.h).
    subghz_environment_set_came_atomo_rainbow_table_file_name(instance->environment, NULL);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(instance->environment, NULL);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(instance->environment, NULL);
}

static void radiogeddon_subghz_environment_release(RadioGeddonSubGhz* instance) {
    if(!instance->environment) return;
    if(instance->rx_running || instance->transmitter) return; // still in use
    subghz_environment_free(instance->environment);
    instance->environment = NULL;
}

static void radiogeddon_subghz_decoders_alloc(RadioGeddonSubGhz* instance) {
    radiogeddon_subghz_environment_acquire(instance, true);

    instance->receiver = subghz_receiver_alloc_init(instance->environment);
    subghz_receiver_set_filter(instance->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(
        instance->receiver, radiogeddon_subghz_receiver_callback, instance);

    instance->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(instance->worker, radiogeddon_subghz_overrun_callback);
    subghz_worker_set_pair_callback(instance->worker, radiogeddon_subghz_pair_callback);
    subghz_worker_set_context(instance->worker, instance);
}

static void radiogeddon_subghz_decoders_free(RadioGeddonSubGhz* instance) {
    if(instance->worker) {
        subghz_worker_free(instance->worker);
        instance->worker = NULL;
    }
    if(instance->receiver) {
        subghz_receiver_free(instance->receiver);
        instance->receiver = NULL;
    }
    radiogeddon_subghz_environment_release(instance);
}

/* 5 V on GPIO pin 1 for the external module, the way the Sub-GHz app does it. */
static void radiogeddon_subghz_ext_power(RadioGeddonSubGhz* instance, bool on) {
    if(on) {
        if(instance->ext_power_ours || furi_hal_power_is_otg_enabled()) return;
        Power* power = furi_record_open(RECORD_POWER);
        power_enable_otg(power, true);
        furi_record_close(RECORD_POWER);
        instance->ext_power_ours = true;
    } else if(instance->ext_power_ours) {
        Power* power = furi_record_open(RECORD_POWER);
        power_enable_otg(power, false);
        furi_record_close(RECORD_POWER);
        instance->ext_power_ours = false;
    }
}

RadioGeddonSubGhz* radiogeddon_subghz_alloc(void) {
    RadioGeddonSubGhz* instance = malloc(sizeof(RadioGeddonSubGhz));
    memset(instance, 0, sizeof(RadioGeddonSubGhz));

    instance->frequency = RADIOGEDDON_FREQUENCY_DEFAULT;
    instance->preset_index = 1; // AM 650 — a sensible general default
    instance->rec_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    subghz_devices_init();
    instance->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);

    return instance;
}

void radiogeddon_subghz_free(RadioGeddonSubGhz* instance) {
    furi_assert(instance);
    if(instance->rx_running) radiogeddon_subghz_rx_stop(instance);
    if(instance->tx_mode != RadioGeddonTxModeNone) radiogeddon_subghz_tx_stop(instance);
    radiogeddon_subghz_ext_power(instance, false);

    radiogeddon_subghz_decoders_free(instance);
    radiogeddon_subghz_environment_release(instance);
    subghz_devices_deinit();

    radiogeddon_subghz_record_discard(instance);
    furi_mutex_free(instance->rec_mutex);
    free(instance);
}

bool radiogeddon_subghz_is_device_present(RadioGeddonSubGhz* instance) {
    if(!instance->device) return false;
    // NOTE: subghz_devices_begin() returns the device's interconnect begin()
    // result, which is NULL (=> false) for the INTERNAL cc1101 because it needs
    // no power-up step. Gating presence on that return value therefore reports
    // the built-in radio as "absent" on every real device. Presence is instead
    // is_connect() after an (ignored-return) begin; for external modules begin
    // powers them up so is_connect() can probe the SPI link.
    subghz_devices_begin(instance->device);
    bool connected = subghz_devices_is_connect(instance->device);
    subghz_devices_end(instance->device);
    return connected;
}

RadioGeddonRadio radiogeddon_subghz_set_radio(
    RadioGeddonSubGhz* instance,
    RadioGeddonRadio radio,
    bool ext_power) {
    furi_check(!instance->rx_running && instance->tx_mode == RadioGeddonTxModeNone);
    furi_check(!instance->device_begun);

    const SubGhzDevice* ext = NULL;
    if(radio == RadioGeddonRadioExternal) {
        if(ext_power) radiogeddon_subghz_ext_power(instance, true);
        // The driver is a plugin on the SD card; is_connect() probes the chip
        // over SPI without keeping it initialised.
        ext = subghz_devices_get_by_name(RADIOGEDDON_EXT_RADIO_NAME);
        if(ext && !subghz_devices_is_connect(ext)) ext = NULL;
        FURI_LOG_I(TAG, "External radio %s", ext ? "found" : "not found");
    }
    if(ext) {
        instance->device = ext;
        instance->radio = RadioGeddonRadioExternal;
    } else {
        radiogeddon_subghz_ext_power(instance, false);
        instance->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio = RadioGeddonRadioInternal;
    }
    return instance->radio;
}

RadioGeddonRadio radiogeddon_subghz_get_radio(RadioGeddonSubGhz* instance) {
    return instance->radio;
}

const char* radiogeddon_subghz_device_name(RadioGeddonSubGhz* instance) {
    if(!instance->device) return "none";
    const char* name = subghz_devices_get_name(instance->device);
    return name ? name : "unknown";
}

void radiogeddon_subghz_set_frequency(RadioGeddonSubGhz* instance, uint32_t frequency) {
    instance->frequency = frequency;
}

uint32_t radiogeddon_subghz_get_frequency(RadioGeddonSubGhz* instance) {
    return instance->frequency;
}

void radiogeddon_subghz_set_preset(RadioGeddonSubGhz* instance, uint8_t preset_index) {
    if(preset_index < radiogeddon_presets_count) instance->preset_index = preset_index;
}

bool radiogeddon_subghz_is_frequency_allowed(RadioGeddonSubGhz* instance, uint32_t frequency) {
    if(!instance->device) return false;
    if(!subghz_devices_is_frequency_valid(instance->device, frequency)) return false;
    return true;
}

/* ---- Receive ----------------------------------------------------------- */

void radiogeddon_subghz_rx_start(
    RadioGeddonSubGhz* instance,
    RadioGeddonSubGhzDecodeCallback decode_callback,
    void* context) {
    furi_assert(instance);
    if(instance->rx_running) return;
    if(!instance->device) return;

    // Guard against an out-of-band frequency: set_frequency asserts on invalid
    // values, so fall back to the default rather than crash.
    if(!subghz_devices_is_frequency_valid(instance->device, instance->frequency)) {
        instance->frequency = RADIOGEDDON_FREQUENCY_DEFAULT;
    }

    instance->decode_cb = decode_callback;
    instance->decode_ctx = context;

    // A new session starts clean: a capture from a previous session was
    // either saved already or abandoned, so delete what is left of it.
    radiogeddon_subghz_record_discard(instance);

    // Measure what the session takes: decoders, keystore, worker thread.
    uint32_t free_before = memmgr_get_free_heap();
    uint32_t low_before = memmgr_get_minimum_free_heap();
    radiogeddon_subghz_decoders_alloc(instance);
    subghz_receiver_reset(instance->receiver);

    subghz_devices_begin(instance->device);
    instance->device_begun = true;
    subghz_devices_reset(instance->device);
    subghz_devices_idle(instance->device);
    subghz_devices_load_preset(
        instance->device, radiogeddon_subghz_current_preset(instance), NULL);
    instance->frequency = subghz_devices_set_frequency(instance->device, instance->frequency);
    subghz_devices_flush_rx(instance->device);

    // Order matches the firmware Sub-GHz subsystem: begin async capture first,
    // then start the worker that drains it. The device enters RX inside
    // start_async_rx — never call set_rx after it (that is for FIFO/sync mode).
    subghz_devices_start_async_rx(instance->device, subghz_worker_rx_callback, instance->worker);
    subghz_worker_start(instance->worker);

    instance->session_cost = rg_mem_session_cost(
        free_before, low_before, memmgr_get_free_heap(), memmgr_get_minimum_free_heap());
    FURI_LOG_I(TAG, "Receive session took %lu bytes", (unsigned long)instance->session_cost);
    instance->rx_running = true;
}

void radiogeddon_subghz_rx_stop(RadioGeddonSubGhz* instance) {
    furi_assert(instance);
    if(!instance->rx_running) return;

    // Write out an active capture; it stays pending for the save screen.
    radiogeddon_subghz_record_stop(instance);

    // Stop the worker before the async capture (reverse of start order), so no
    // pair callback fires into a half-torn-down device.
    if(subghz_worker_is_running(instance->worker)) subghz_worker_stop(instance->worker);
    subghz_devices_stop_async_rx(instance->device);
    subghz_devices_idle(instance->device);
    subghz_devices_sleep(instance->device);
    if(instance->device_begun) {
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }

    instance->rx_running = false;
    instance->decode_cb = NULL;
    instance->decode_ctx = NULL;

    radiogeddon_subghz_decoders_free(instance);
}

bool radiogeddon_subghz_is_rx_running(RadioGeddonSubGhz* instance) {
    return instance->rx_running;
}

uint32_t radiogeddon_subghz_session_cost(RadioGeddonSubGhz* instance) {
    return instance->session_cost;
}

void radiogeddon_subghz_rx_retune(RadioGeddonSubGhz* instance, uint32_t frequency) {
    if(!instance->rx_running || !instance->device) return;
    if(!subghz_devices_is_frequency_valid(instance->device, frequency)) return;
    if(frequency == instance->frequency) return;

    // Same stop/retune/start dance as the firmware hopper: the device stays
    // powered (begun); only the async capture + worker cycle and the decoder
    // state is reset so timing from the old frequency cannot bleed across.
    if(subghz_worker_is_running(instance->worker)) subghz_worker_stop(instance->worker);
    subghz_devices_stop_async_rx(instance->device);
    subghz_devices_idle(instance->device);
    instance->frequency = subghz_devices_set_frequency(instance->device, frequency);
    subghz_devices_flush_rx(instance->device);
    subghz_receiver_reset(instance->receiver);
    subghz_devices_start_async_rx(instance->device, subghz_worker_rx_callback, instance->worker);
    subghz_worker_start(instance->worker);
}

float radiogeddon_subghz_get_rssi(RadioGeddonSubGhz* instance) {
    if(!instance->device || !instance->rx_running) return -127.0f;
    return subghz_devices_get_rssi(instance->device);
}

void radiogeddon_subghz_scan_begin(RadioGeddonSubGhz* instance) {
    if(!instance->device || instance->device_begun || instance->rx_running) return;
    subghz_devices_begin(instance->device);
    subghz_devices_reset(instance->device);
    subghz_devices_load_preset(
        instance->device, radiogeddon_subghz_current_preset(instance), NULL);
    instance->device_begun = true;
}

void radiogeddon_subghz_scan_end(RadioGeddonSubGhz* instance) {
    if(!instance->device || !instance->device_begun || instance->rx_running) return;
    subghz_devices_idle(instance->device);
    subghz_devices_sleep(instance->device);
    subghz_devices_end(instance->device);
    instance->device_begun = false;
}

float radiogeddon_subghz_probe_rssi(RadioGeddonSubGhz* instance, uint32_t frequency) {
    return radiogeddon_subghz_probe_rssi_dwell(instance, frequency, 0);
}

float radiogeddon_subghz_probe_rssi_dwell(
    RadioGeddonSubGhz* instance,
    uint32_t frequency,
    uint32_t dwell_ms) {
    if(!instance->device) return -127.0f;
    // set_frequency asserts on out-of-band values — never probe an invalid one.
    if(!subghz_devices_is_frequency_valid(instance->device, frequency)) return -127.0f;
    bool temp_session = !instance->device_begun;
    if(temp_session) {
        subghz_devices_begin(instance->device);
        subghz_devices_reset(instance->device);
        subghz_devices_load_preset(
            instance->device, radiogeddon_subghz_current_preset(instance), NULL);
    }
    subghz_devices_idle(instance->device);
    subghz_devices_set_frequency(instance->device, frequency);
    subghz_devices_set_rx(instance->device);
    furi_delay_ms(3); // let the AGC settle
    float rssi = subghz_devices_get_rssi(instance->device);
    // Keep listening for the dwell time and report the strongest reading, so a
    // short burst inside the window is not missed between two samples.
    uint32_t start = furi_get_tick();
    while(furi_get_tick() - start < furi_ms_to_ticks(dwell_ms)) {
        furi_delay_ms(1);
        float r = subghz_devices_get_rssi(instance->device);
        if(r > rssi) rssi = r;
    }
    subghz_devices_idle(instance->device);
    if(temp_session) {
        subghz_devices_sleep(instance->device);
        subghz_devices_end(instance->device);
    }
    return rssi;
}

/* ---- RAW recording (streaming) ----------------------------------------- */

// Detach the recorder from the worker thread and wait until no callback still
// uses it. The stats at this moment stand in for readers until it finishes.
static RadioGeddonRecorder* radiogeddon_subghz_record_detach(RadioGeddonSubGhz* instance) {
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    RadioGeddonRecorder* rec = instance->recorder;
    if(rec) {
        radiogeddon_recorder_stats(rec, &instance->rec_last);
        __atomic_store_n(&instance->recorder, NULL, __ATOMIC_SEQ_CST);
    }
    furi_mutex_release(instance->rec_mutex);
    while(rec && __atomic_load_n(&instance->rec_busy, __ATOMIC_SEQ_CST)) {
        furi_delay_tick(1);
    }
    return rec;
}

static void radiogeddon_subghz_record_remove_temp(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_remove(storage, RADIOGEDDON_RECORD_TEMP);
    furi_record_close(RECORD_STORAGE);
}

RadioGeddonRecordError radiogeddon_subghz_record_start(RadioGeddonSubGhz* instance) {
    if(!instance->rx_running || instance->recorder) return RadioGeddonRecordOpenFailed;
    radiogeddon_subghz_record_discard(instance);

    RadioGeddonRecorder* rec = radiogeddon_recorder_alloc();
    if(!rec) {
        memset(&instance->rec_last, 0, sizeof(instance->rec_last));
        instance->rec_last.error = RadioGeddonRecordNoMemory;
        return RadioGeddonRecordNoMemory;
    }
    // Attach first: the ring takes samples while the file is being created.
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    __atomic_store_n(&instance->recorder, rec, __ATOMIC_SEQ_CST);
    furi_mutex_release(instance->rec_mutex);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    RadioGeddonRecordError err = radiogeddon_recorder_open(
        rec,
        storage,
        RADIOGEDDON_RECORD_TEMP,
        instance->frequency,
        radiogeddon_presets[instance->preset_index].file_name);
    furi_record_close(RECORD_STORAGE);

    if(err != RadioGeddonRecordOk) {
        radiogeddon_subghz_record_detach(instance);
        RadioGeddonRecordStats st;
        radiogeddon_recorder_finish(rec, &st);
        st.error = err;
        furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
        instance->rec_last = st;
        furi_mutex_release(instance->rec_mutex);
        radiogeddon_subghz_record_remove_temp();
    }
    return err;
}

void radiogeddon_subghz_record_stop(RadioGeddonSubGhz* instance) {
    RadioGeddonRecorder* rec = radiogeddon_subghz_record_detach(instance);
    if(!rec) return;
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    // An empty or broken file is not offered for saving.
    bool keep = st.samples > 0 && st.error == RadioGeddonRecordOk;
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    instance->rec_last = st;
    instance->rec_pending = keep;
    furi_mutex_release(instance->rec_mutex);
    if(!keep) radiogeddon_subghz_record_remove_temp();
}

bool radiogeddon_subghz_is_recording(RadioGeddonSubGhz* instance) {
    return __atomic_load_n(&instance->recorder, __ATOMIC_ACQUIRE) != NULL;
}

bool radiogeddon_subghz_record_status(RadioGeddonSubGhz* instance, RadioGeddonRecordStats* out) {
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    bool active = instance->recorder != NULL;
    if(active) {
        radiogeddon_recorder_stats(instance->recorder, out);
    } else {
        *out = instance->rec_last;
    }
    furi_mutex_release(instance->rec_mutex);
    return active;
}

bool radiogeddon_subghz_record_pending(RadioGeddonSubGhz* instance) {
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    bool pending = instance->rec_pending;
    furi_mutex_release(instance->rec_mutex);
    return pending;
}

bool radiogeddon_subghz_record_save(RadioGeddonSubGhz* instance, const char* path) {
    if(!radiogeddon_subghz_record_pending(instance)) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = storage_common_rename(storage, RADIOGEDDON_RECORD_TEMP, path) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(ok) {
        furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
        instance->rec_pending = false;
        furi_mutex_release(instance->rec_mutex);
    }
    return ok;
}

void radiogeddon_subghz_record_discard(RadioGeddonSubGhz* instance) {
    if(!radiogeddon_subghz_record_pending(instance)) return;
    radiogeddon_subghz_record_remove_temp();
    furi_mutex_acquire(instance->rec_mutex, FuriWaitForever);
    instance->rec_pending = false;
    furi_mutex_release(instance->rec_mutex);
}

/* ---- Transmit / replay ------------------------------------------------- */

static void radiogeddon_subghz_file_encoder_end(void* context) {
    RadioGeddonSubGhz* instance = context;
    if(instance->tx_complete_cb) instance->tx_complete_cb(instance->tx_complete_ctx);
}

RadioGeddonTxResult radiogeddon_subghz_tx_start(
    RadioGeddonSubGhz* instance,
    const char* file_path,
    RadioGeddonTxCompleteCallback complete_cb,
    void* context) {
    if(!instance->device) return RadioGeddonTxErrorNoDevice;
    if(instance->rx_running || instance->tx_mode != RadioGeddonTxModeNone)
        return RadioGeddonTxErrorBusy;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    FuriString* temp_str = furi_string_alloc();
    uint32_t temp_u32 = 0;
    uint32_t frequency = instance->frequency;
    RadioGeddonTxResult result = RadioGeddonTxErrorParse;
    bool is_raw = false;
    uint8_t* custom_preset_data = NULL; // register array for a FuriHalSubGhzPresetCustom file

    do {
        if(!flipper_format_file_open_existing(ff, file_path)) {
            result = RadioGeddonTxErrorNoFile;
            break;
        }
        if(!flipper_format_read_header(ff, temp_str, &temp_u32)) break;
        if(!flipper_format_read_uint32(ff, "Frequency", &frequency, 1)) {
            frequency = instance->frequency;
        }
        if(!radiogeddon_subghz_is_frequency_allowed(instance, frequency)) {
            result = RadioGeddonTxErrorRegion;
            break;
        }
        // The external module's driver comes from the SD card and differs
        // between firmwares; ask the firmware's region table directly too, so
        // TX through it is never less restricted than the internal radio.
        if(instance->radio == RadioGeddonRadioExternal &&
           !furi_hal_region_is_frequency_allowed(frequency)) {
            result = RadioGeddonTxErrorRegion;
            break;
        }
        // An external module can be unplugged at any time: check it answers
        // before driving it (its frequency setup waits for the chip).
        if(instance->radio == RadioGeddonRadioExternal &&
           !subghz_devices_is_connect(instance->device)) {
            result = RadioGeddonTxErrorNoDevice;
            break;
        }
        // Read the preset so the EXACT modulation the signal was captured on is
        // reproduced. Transmitting a capture on the wrong modulation would emit
        // nothing usable, so an unrecognised preset is refused rather than
        // silently sent on a default modulation.
        FuriHalSubGhzPreset preset_enum = FuriHalSubGhzPresetIDLE;
        bool preset_known = false;
        if(flipper_format_read_string(ff, "Preset", temp_str)) {
            for(size_t i = 0; i < radiogeddon_presets_count; i++) {
                if(furi_string_equal_str(temp_str, radiogeddon_presets[i].file_name)) {
                    preset_enum = radiogeddon_presets[i].preset;
                    preset_known = true;
                    break;
                }
            }
            if(!preset_known && furi_string_equal_str(temp_str, "FuriHalSubGhzPresetCustom")) {
                // Custom preset: load the raw CC1101 register array from the file
                // (as stock-app .sub captures commonly store it).
                uint32_t count = 0;
                flipper_format_rewind(ff);
                if(flipper_format_get_value_count(ff, "Custom_preset_data", &count) && count > 0 &&
                   count <= 512) {
                    custom_preset_data = malloc(count);
                    flipper_format_rewind(ff);
                    if(flipper_format_read_hex(
                           ff, "Custom_preset_data", custom_preset_data, (uint16_t)count)) {
                        preset_enum = FuriHalSubGhzPresetCustom;
                        preset_known = true;
                    } else {
                        free(custom_preset_data);
                        custom_preset_data = NULL;
                    }
                }
            }
        }
        if(!preset_known) {
            result = RadioGeddonTxErrorParse; // unknown/missing modulation -> refuse
            break;
        }
        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Protocol", temp_str)) break;

        if(furi_string_equal_str(temp_str, "RAW")) {
            is_raw = true;
        } else {
            // Refuse to replay rolling-code / protected protocols.
            const SubGhzProtocol* proto = subghz_protocol_registry_get_by_name(
                &subghz_protocol_registry, furi_string_get_cstr(temp_str));
            if(!proto || !(proto->flag & SubGhzProtocolFlag_Send)) {
                result = RadioGeddonTxErrorProtected;
                break;
            }
            if(proto->type == SubGhzProtocolTypeDynamic) {
                result = RadioGeddonTxErrorProtected;
                break;
            }
        }

        // Prepare radio.
        subghz_devices_begin(instance->device);
        instance->device_begun = true;
        subghz_devices_reset(instance->device);
        subghz_devices_idle(instance->device);
        subghz_devices_load_preset(instance->device, preset_enum, custom_preset_data);
        frequency = subghz_devices_set_frequency(instance->device, frequency);

        instance->tx_complete_cb = complete_cb;
        instance->tx_complete_ctx = context;

        if(is_raw) {
            instance->file_encoder = subghz_file_encoder_worker_alloc();
            subghz_file_encoder_worker_callback_end(
                instance->file_encoder, radiogeddon_subghz_file_encoder_end, instance);
            if(!subghz_file_encoder_worker_start(
                   instance->file_encoder, file_path, subghz_devices_get_name(instance->device))) {
                subghz_file_encoder_worker_free(instance->file_encoder);
                instance->file_encoder = NULL;
                break;
            }
            furi_delay_ms(100); // let the worker prime its buffer
            // Region gate: set_tx returns false when TX is forbidden here.
            if(!subghz_devices_set_tx(instance->device)) {
                subghz_file_encoder_worker_stop(instance->file_encoder);
                subghz_file_encoder_worker_free(instance->file_encoder);
                instance->file_encoder = NULL;
                result = RadioGeddonTxErrorRegion;
                break;
            }
            if(!subghz_devices_start_async_tx(
                   instance->device,
                   subghz_file_encoder_worker_get_level_duration,
                   instance->file_encoder)) {
                subghz_file_encoder_worker_stop(instance->file_encoder);
                subghz_file_encoder_worker_free(instance->file_encoder);
                instance->file_encoder = NULL;
                result = RadioGeddonTxErrorRegion;
                break;
            }
            instance->tx_mode = RadioGeddonTxModeRaw;
            result = RadioGeddonTxOk;
        } else {
            // Work on an in-memory copy so the saved .sub file is never
            // mutated. Ensure a bounded "Repeat" so a single press-worth of
            // frames is emitted even if the file omits it.
            FlipperFormat* mem_ff = flipper_format_string_alloc();
            Stream* src = flipper_format_get_raw_stream(ff);
            Stream* dst = flipper_format_get_raw_stream(mem_ff);
            flipper_format_rewind(ff);
            stream_rewind(src);
            stream_copy_full(src, dst);
            uint32_t repeat = 10;
            flipper_format_rewind(mem_ff);
            flipper_format_insert_or_update_uint32(mem_ff, "Repeat", &repeat, 1);
            flipper_format_rewind(mem_ff);

            radiogeddon_subghz_environment_acquire(instance, false);
            instance->transmitter = subghz_transmitter_alloc_init(
                instance->environment, furi_string_get_cstr(temp_str));
            if(!instance->transmitter) {
                flipper_format_free(mem_ff);
                break;
            }
            SubGhzProtocolStatus ds =
                subghz_transmitter_deserialize(instance->transmitter, mem_ff);
            flipper_format_free(mem_ff);
            if(ds != SubGhzProtocolStatusOk) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
                break;
            }
            if(!subghz_devices_set_tx(instance->device)) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
                result = RadioGeddonTxErrorRegion;
                break;
            }
            if(!subghz_devices_start_async_tx(
                   instance->device, subghz_transmitter_yield, instance->transmitter)) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
                result = RadioGeddonTxErrorRegion;
                break;
            }
            instance->tx_mode = RadioGeddonTxModeProtocol;
            result = RadioGeddonTxOk;
        }
        instance->frequency = frequency;
    } while(false);

    if(result != RadioGeddonTxOk && instance->device_begun &&
       instance->tx_mode == RadioGeddonTxModeNone) {
        subghz_devices_sleep(instance->device);
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }

    if(result != RadioGeddonTxOk) radiogeddon_subghz_environment_release(instance);

    if(custom_preset_data) free(custom_preset_data); // consumed by load_preset already
    furi_string_free(temp_str);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return result;
}

bool radiogeddon_subghz_is_tx_running(RadioGeddonSubGhz* instance) {
    if(instance->tx_mode == RadioGeddonTxModeNone) return false;
    return !subghz_devices_is_async_complete_tx(instance->device);
}

void radiogeddon_subghz_tx_stop(RadioGeddonSubGhz* instance) {
    if(instance->tx_mode == RadioGeddonTxModeNone) return;

    subghz_devices_stop_async_tx(instance->device);

    if(instance->tx_mode == RadioGeddonTxModeRaw && instance->file_encoder) {
        if(subghz_file_encoder_worker_is_running(instance->file_encoder))
            subghz_file_encoder_worker_stop(instance->file_encoder);
        subghz_file_encoder_worker_free(instance->file_encoder);
        instance->file_encoder = NULL;
    }
    if(instance->tx_mode == RadioGeddonTxModeProtocol && instance->transmitter) {
        subghz_transmitter_free(instance->transmitter);
        instance->transmitter = NULL;
    }
    radiogeddon_subghz_environment_release(instance);

    subghz_devices_idle(instance->device);
    subghz_devices_sleep(instance->device);
    if(instance->device_begun) {
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }
    instance->tx_mode = RadioGeddonTxModeNone;
    instance->tx_complete_cb = NULL;
    instance->tx_complete_ctx = NULL;
}
