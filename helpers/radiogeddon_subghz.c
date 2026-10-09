#include "radiogeddon_subghz.h"
#include "radiogeddon_storage.h"

#include <furi_hal_subghz.h>
#include <furi_hal_region.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>

#define TAG "RadioGeddonSubGhz"

/** Maximum RAW samples buffered per capture (level-signed durations). */
#define RADIOGEDDON_RAW_CAPACITY (16384u)
/** How many RAW values to emit per RAW_Data line in the saved file. */
#define RADIOGEDDON_RAW_LINE_VALUES (512u)

const RadioGeddonPreset radiogeddon_presets[] = {
    {"AM 270", "FuriHalSubGhzPresetOok270Async", FuriHalSubGhzPresetOok270Async},
    {"AM 650", "FuriHalSubGhzPresetOok650Async", FuriHalSubGhzPresetOok650Async},
    {"FM 2.38k", "FuriHalSubGhzPreset2FSKDev238Async", FuriHalSubGhzPreset2FSKDev238Async},
    {"FM 47.6k", "FuriHalSubGhzPreset2FSKDev476Async", FuriHalSubGhzPreset2FSKDev476Async},
};
const size_t radiogeddon_presets_count =
    sizeof(radiogeddon_presets) / sizeof(radiogeddon_presets[0]);

// Common Sub-GHz frequencies (Hz). Region validity is checked before use.
const uint32_t radiogeddon_frequencies[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000,
    390000000, 418000000, 433075000, 433420000, 433920000, 434420000,
    434775000, 438900000, 464000000, 779000000, 868350000, 915000000,
    925000000,
};
const size_t radiogeddon_frequencies_count =
    sizeof(radiogeddon_frequencies) / sizeof(radiogeddon_frequencies[0]);

typedef enum {
    RadioGeddonTxModeNone,
    RadioGeddonTxModeProtocol,
    RadioGeddonTxModeRaw,
} RadioGeddonTxMode;

struct RadioGeddonSubGhz {
    const SubGhzDevice* device;
    bool device_begun;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    void* raw_decoder; // SubGhzProtocolDecoderRAW* (unused by our own capture)

    uint32_t frequency;
    uint8_t preset_index;

    bool rx_running;
    RadioGeddonSubGhzDecodeCallback decode_cb;
    void* decode_ctx;

    // Own RAW capture buffer (populated from the worker thread)
    bool recording;
    bool record_overflow;
    int32_t* raw_buffer;
    volatile size_t raw_count;

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

// Worker reports a capture buffer overrun: drop partial decode state.
static void radiogeddon_subghz_overrun_callback(void* context) {
    RadioGeddonSubGhz* instance = context;
    if(instance->receiver) subghz_receiver_reset(instance->receiver);
}

// Called from the worker thread for every (level,duration) pair received.
static void radiogeddon_subghz_pair_callback(void* context, bool level, uint32_t duration) {
    RadioGeddonSubGhz* instance = context;

    // Feed the protocol decoders (live known-protocol identification).
    if(instance->receiver) {
        subghz_receiver_decode(instance->receiver, level, duration);
    }

    // Append to our RAW capture buffer if recording.
    if(instance->recording) {
        size_t idx = instance->raw_count;
        if(idx < RADIOGEDDON_RAW_CAPACITY) {
            uint32_t d = duration;
            if(d > (uint32_t)INT32_MAX) d = (uint32_t)INT32_MAX;
            instance->raw_buffer[idx] = level ? (int32_t)d : -(int32_t)d;
            instance->raw_count = idx + 1;
        } else {
            instance->record_overflow = true;
        }
    }
}

// Called by the receiver when a protocol is fully decoded.
static void radiogeddon_subghz_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    RadioGeddonSubGhz* instance = context;

    FuriString* text = furi_string_alloc();
    FuriString* serialized = furi_string_alloc();

    bool have_text = subghz_protocol_decoder_base_get_string(decoder_base, text);
    uint8_t hash = subghz_protocol_decoder_base_get_hash_data(decoder_base);

    // Produce a complete, loadable .sub representation in RAM.
    SubGhzRadioPreset preset = radiogeddon_subghz_build_radio_preset(instance);
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
        instance->decode_cb(
            name, hash, text, have_sub ? serialized : NULL, instance->decode_ctx);
    }

    furi_string_free(text);
    furi_string_free(serialized);

    subghz_receiver_reset(receiver);
}

/* ---- Lifecycle --------------------------------------------------------- */

RadioGeddonSubGhz* radiogeddon_subghz_alloc(void) {
    RadioGeddonSubGhz* instance = malloc(sizeof(RadioGeddonSubGhz));
    memset(instance, 0, sizeof(RadioGeddonSubGhz));

    instance->frequency = RADIOGEDDON_FREQUENCY_DEFAULT;
    instance->preset_index = 1; // AM 650 — a sensible general default
    instance->raw_buffer = malloc(sizeof(int32_t) * RADIOGEDDON_RAW_CAPACITY);

    subghz_devices_init();
    instance->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);

    instance->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)&subghz_protocol_registry);
    // Best-effort keystore load improves KeeLoq manufacturer identification.
    subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_set_came_atomo_rainbow_table_file_name(instance->environment, NULL);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(instance->environment, NULL);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(instance->environment, NULL);

    instance->receiver = subghz_receiver_alloc_init(instance->environment);
    subghz_receiver_set_filter(instance->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(
        instance->receiver, radiogeddon_subghz_receiver_callback, instance);

    instance->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        instance->worker, radiogeddon_subghz_overrun_callback);
    subghz_worker_set_pair_callback(instance->worker, radiogeddon_subghz_pair_callback);
    subghz_worker_set_context(instance->worker, instance);

    return instance;
}

void radiogeddon_subghz_free(RadioGeddonSubGhz* instance) {
    furi_assert(instance);
    if(instance->rx_running) radiogeddon_subghz_rx_stop(instance);
    if(instance->tx_mode != RadioGeddonTxModeNone) radiogeddon_subghz_tx_stop(instance);

    subghz_worker_free(instance->worker);
    subghz_receiver_free(instance->receiver);
    subghz_environment_free(instance->environment);
    subghz_devices_deinit();

    free(instance->raw_buffer);
    free(instance);
}

bool radiogeddon_subghz_is_device_present(RadioGeddonSubGhz* instance) {
    if(!instance->device) return false;
    bool begun = subghz_devices_begin(instance->device);
    bool connected = begun && subghz_devices_is_connect(instance->device);
    if(begun) subghz_devices_end(instance->device);
    return connected;
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

    instance->decode_cb = decode_callback;
    instance->decode_ctx = context;

    subghz_receiver_reset(instance->receiver);

    subghz_devices_begin(instance->device);
    instance->device_begun = true;
    subghz_devices_reset(instance->device);
    subghz_devices_idle(instance->device);
    subghz_devices_load_preset(
        instance->device, radiogeddon_subghz_current_preset(instance), NULL);
    instance->frequency = subghz_devices_set_frequency(instance->device, instance->frequency);

    subghz_worker_start(instance->worker);
    subghz_devices_start_async_rx(
        instance->device, subghz_worker_rx_callback, instance->worker);
    subghz_devices_set_rx(instance->device);

    instance->rx_running = true;
}

void radiogeddon_subghz_rx_stop(RadioGeddonSubGhz* instance) {
    furi_assert(instance);
    if(!instance->rx_running) return;

    if(instance->recording) radiogeddon_subghz_record_stop(instance);

    subghz_devices_stop_async_rx(instance->device);
    if(subghz_worker_is_running(instance->worker)) subghz_worker_stop(instance->worker);
    subghz_devices_idle(instance->device);
    subghz_devices_sleep(instance->device);
    if(instance->device_begun) {
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }

    instance->rx_running = false;
    instance->decode_cb = NULL;
    instance->decode_ctx = NULL;
}

bool radiogeddon_subghz_is_rx_running(RadioGeddonSubGhz* instance) {
    return instance->rx_running;
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
    if(!instance->device) return -127.0f;
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
    subghz_devices_idle(instance->device);
    if(temp_session) {
        subghz_devices_sleep(instance->device);
        subghz_devices_end(instance->device);
    }
    return rssi;
}

/* ---- RAW recording (own buffer) ---------------------------------------- */

bool radiogeddon_subghz_record_start(RadioGeddonSubGhz* instance, const char* file_path) {
    UNUSED(file_path); // path is used at stop time when flushing
    if(!instance->rx_running) return false;
    if(instance->recording) return false;
    instance->raw_count = 0;
    instance->record_overflow = false;
    instance->recording = true;
    return true;
}

bool radiogeddon_subghz_is_recording(RadioGeddonSubGhz* instance) {
    return instance->recording;
}

size_t radiogeddon_subghz_record_sample_count(RadioGeddonSubGhz* instance) {
    return instance->raw_count;
}

bool radiogeddon_subghz_record_overflowed(RadioGeddonSubGhz* instance) {
    return instance->record_overflow;
}

void radiogeddon_subghz_record_stop(RadioGeddonSubGhz* instance) {
    if(!instance->recording) return;
    instance->recording = false;
}

/**
 * Flush the captured RAW buffer to a .sub file. Declared in storage helper so
 * scenes can call it after stopping the capture; implemented here where the
 * buffer lives.
 */
bool radiogeddon_subghz_record_flush_to_file(RadioGeddonSubGhz* instance, const char* file_path) {
    if(instance->raw_count == 0) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, file_path)) break;
        if(!flipper_format_write_header_cstr(ff, RADIOGEDDON_RAW_FILE_TYPE_STR, 1)) break;
        uint32_t freq = instance->frequency;
        if(!flipper_format_write_uint32(ff, "Frequency", &freq, 1)) break;
        if(!flipper_format_write_string_cstr(
               ff, "Preset", radiogeddon_presets[instance->preset_index].file_name))
            break;
        if(!flipper_format_write_string_cstr(ff, "Protocol", "RAW")) break;

        size_t total = instance->raw_count;
        size_t written = 0;
        FuriString* line = furi_string_alloc();
        bool line_ok = true;
        while(written < total && line_ok) {
            size_t chunk = total - written;
            if(chunk > RADIOGEDDON_RAW_LINE_VALUES) chunk = RADIOGEDDON_RAW_LINE_VALUES;
            furi_string_reset(line);
            for(size_t i = 0; i < chunk; i++) {
                furi_string_cat_printf(
                    line, (i == 0) ? "%ld" : " %ld", (long)instance->raw_buffer[written + i]);
            }
            line_ok = flipper_format_write_string_cstr(ff, "RAW_Data", furi_string_get_cstr(line));
            written += chunk;
        }
        furi_string_free(line);
        ok = line_ok;
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
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
        // Read preset so the correct modulation is loaded.
        FuriHalSubGhzPreset preset_enum = radiogeddon_subghz_current_preset(instance);
        if(flipper_format_read_string(ff, "Preset", temp_str)) {
            for(size_t i = 0; i < radiogeddon_presets_count; i++) {
                if(furi_string_equal_str(temp_str, radiogeddon_presets[i].file_name)) {
                    preset_enum = radiogeddon_presets[i].preset;
                    break;
                }
            }
        }
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
        subghz_devices_load_preset(instance->device, preset_enum, NULL);
        frequency = subghz_devices_set_frequency(instance->device, frequency);

        instance->tx_complete_cb = complete_cb;
        instance->tx_complete_ctx = context;

        if(is_raw) {
            instance->file_encoder = subghz_file_encoder_worker_alloc();
            subghz_file_encoder_worker_callback_end(
                instance->file_encoder, radiogeddon_subghz_file_encoder_end, instance);
            if(!subghz_file_encoder_worker_start(
                   instance->file_encoder,
                   file_path,
                   subghz_devices_get_name(instance->device))) {
                subghz_file_encoder_worker_free(instance->file_encoder);
                instance->file_encoder = NULL;
                break;
            }
            furi_delay_ms(100); // let the worker prime its buffer
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
            flipper_format_rewind(ff);
            instance->transmitter =
                subghz_transmitter_alloc_init(instance->environment, furi_string_get_cstr(temp_str));
            if(!instance->transmitter) break;
            if(subghz_transmitter_deserialize(instance->transmitter, ff) !=
               SubGhzProtocolStatusOk) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
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

    if(result != RadioGeddonTxOk && instance->device_begun && instance->tx_mode == RadioGeddonTxModeNone) {
        subghz_devices_sleep(instance->device);
        subghz_devices_end(instance->device);
        instance->device_begun = false;
    }

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
