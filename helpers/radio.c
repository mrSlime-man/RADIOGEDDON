#include "radio.h"

#include <furi_hal_region.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/protocols/base.h>
#include <lib/flipper_format/flipper_format.h>

#define TAG "RadioGeddon"

/*
 * Only standard presets are offered. This keeps every recording a plain
 * standard-preset .sub (no "Custom_preset_data" block), which means our own
 * recordings always round-trip cleanly through record -> save -> load ->
 * replay, and stay compatible with the stock Sub-GHz app.
 */
const RgRadioPresetInfo rg_radio_presets[] = {
    {FuriHalSubGhzPresetOok650Async, "AM650", "AM650 OOK"},
    {FuriHalSubGhzPresetOok270Async, "AM270", "AM270 OOK"},
    {FuriHalSubGhzPreset2FSKDev238Async, "FM238", "FM238 2-FSK"},
    {FuriHalSubGhzPreset2FSKDev476Async, "FM476", "FM476 2-FSK"},
};
const size_t rg_radio_presets_count = sizeof(rg_radio_presets) / sizeof(rg_radio_presets[0]);
const size_t rg_radio_preset_default_index = 0; /* AM650 - most common for remotes */

struct RgRadio {
    const SubGhzDevice* device;
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    SubGhzProtocolDecoderRAW* raw_decoder;
    SubGhzFileEncoderWorker* tx_worker;

    RgRadioState state;
    bool connected;
    bool recording;

    uint32_t frequency;
    FuriHalSubGhzPreset preset;

    RgRadioDecodeCallback decode_cb;
    void* decode_ctx;
};

/* ---- preset <-> string helpers ------------------------------------------ */

static const char* rg_radio_preset_code(FuriHalSubGhzPreset preset) {
    for(size_t i = 0; i < rg_radio_presets_count; i++) {
        if(rg_radio_presets[i].preset == preset) return rg_radio_presets[i].code;
    }
    return "AM650";
}

/* Map the long enum name stored in a .sub "Preset" field back to the enum. */
static bool rg_radio_preset_from_file_name(const char* name, FuriHalSubGhzPreset* out) {
    if(!strcmp(name, "FuriHalSubGhzPresetOok270Async")) {
        *out = FuriHalSubGhzPresetOok270Async;
    } else if(!strcmp(name, "FuriHalSubGhzPresetOok650Async")) {
        *out = FuriHalSubGhzPresetOok650Async;
    } else if(!strcmp(name, "FuriHalSubGhzPreset2FSKDev238Async")) {
        *out = FuriHalSubGhzPreset2FSKDev238Async;
    } else if(!strcmp(name, "FuriHalSubGhzPreset2FSKDev476Async")) {
        *out = FuriHalSubGhzPreset2FSKDev476Async;
    } else {
        return false;
    }
    return true;
}

/* ---- worker / decoder callbacks ----------------------------------------- */

/* Worker overrun: reset the decoder so a burst of noise can't wedge it. */
static void rg_radio_overrun_callback(void* context) {
    RgRadio* radio = context;
    if(radio && radio->receiver) subghz_receiver_reset(radio->receiver);
}

/* Each captured level/duration pair: feed the live decoder and, when
 * recording, the RAW-to-file decoder. Runs on the worker thread. */
static void rg_radio_worker_pair_callback(void* context, bool level, uint32_t duration) {
    RgRadio* radio = context;
    if(!radio) return;
    if(radio->recording && radio->raw_decoder) {
        subghz_protocol_decoder_raw_feed(radio->raw_decoder, level, duration);
    }
    if(radio->receiver) subghz_receiver_decode(radio->receiver, level, duration);
}

/* A protocol decoded successfully. Hand a copy to the app, then reset. */
static void rg_radio_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    RgRadio* radio = context;
    if(radio && radio->decode_cb && decoder_base) {
        FuriString* text = furi_string_alloc();
        subghz_protocol_decoder_base_get_string(decoder_base, text);
        const char* name = (decoder_base->protocol && decoder_base->protocol->name) ?
                               decoder_base->protocol->name :
                               "Unknown";
        SubGhzProtocolType type = decoder_base->protocol ? decoder_base->protocol->type :
                                                           SubGhzProtocolTypeUnknown;
        radio->decode_cb(radio->decode_ctx, name, type, text);
        furi_string_free(text);
    }
    subghz_receiver_reset(receiver);
}

/* ---- lifecycle ----------------------------------------------------------- */

RgRadio* rg_radio_alloc(void) {
    RgRadio* radio = malloc(sizeof(RgRadio));
    memset(radio, 0, sizeof(RgRadio));

    radio->state = RgRadioStateIdle;
    radio->connected = false;
    radio->recording = false;
    radio->frequency = 433920000;
    radio->preset = rg_radio_presets[rg_radio_preset_default_index].preset;

    radio->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(radio->environment, (void*)&subghz_protocol_registry);

    /* Best-effort load of manufacturer keystores / rainbow tables. Missing
     * files are fine - those specific protocols just won't fully decode. */
    subghz_environment_load_keystore(radio->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(radio->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    subghz_environment_set_came_atomo_rainbow_table_file_name(
        radio->environment, SUBGHZ_CAME_ATOMO_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        radio->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        radio->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);

    radio->receiver = subghz_receiver_alloc_init(radio->environment);
    subghz_receiver_set_filter(radio->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(radio->receiver, rg_radio_receiver_callback, radio);

    radio->raw_decoder = subghz_protocol_decoder_raw_alloc(radio->environment);

    radio->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(radio->worker, rg_radio_overrun_callback);
    subghz_worker_set_pair_callback(radio->worker, rg_radio_worker_pair_callback);
    subghz_worker_set_context(radio->worker, radio);

    return radio;
}

void rg_radio_free(RgRadio* radio) {
    if(!radio) return;
    rg_radio_end(radio);

    if(radio->worker) subghz_worker_free(radio->worker);
    if(radio->raw_decoder) subghz_protocol_decoder_raw_free(radio->raw_decoder);
    if(radio->receiver) subghz_receiver_free(radio->receiver);
    if(radio->environment) subghz_environment_free(radio->environment);
    free(radio);
}

bool rg_radio_begin(RgRadio* radio) {
    if(!radio) return false;
    if(radio->device) return radio->connected; /* already begun */

    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!radio->device) {
        subghz_devices_deinit();
        radio->connected = false;
        FURI_LOG_E(TAG, "Internal radio device not found");
        return false;
    }

    subghz_devices_begin(radio->device);
    radio->connected = subghz_devices_is_connect(radio->device);
    if(!radio->connected) {
        FURI_LOG_E(TAG, "Radio present but not connected");
    }
    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    return radio->connected;
}

void rg_radio_end(RgRadio* radio) {
    if(!radio) return;
    if(radio->state == RgRadioStateRx) rg_radio_stop_rx(radio);
    if(radio->state == RgRadioStateTx) rg_radio_stop_tx(radio);

    if(radio->device) {
        subghz_devices_idle(radio->device);
        subghz_devices_sleep(radio->device);
        subghz_devices_end(radio->device);
        subghz_devices_deinit();
        radio->device = NULL;
    }
    radio->connected = false;
    radio->state = RgRadioStateIdle;
}

/* ---- queries ------------------------------------------------------------- */

bool rg_radio_is_connected(const RgRadio* radio) {
    return radio && radio->connected;
}

RgRadioState rg_radio_get_state(const RgRadio* radio) {
    return radio ? radio->state : RgRadioStateIdle;
}

bool rg_radio_is_frequency_valid(const RgRadio* radio, uint32_t frequency) {
    if(!radio || !radio->device) return false;
    return subghz_devices_is_frequency_valid(radio->device, frequency);
}

bool rg_radio_is_tx_allowed(const RgRadio* radio, uint32_t frequency) {
    UNUSED(radio);
    /* Region policy is owned by the firmware. On official firmware this
     * enforces the local regulatory band; on Unleashed/RogueMaster it is
     * typically unrestricted. We never bypass it - we only ask. */
    return furi_hal_region_is_frequency_allowed(frequency);
}

void rg_radio_set_frequency(RgRadio* radio, uint32_t frequency) {
    if(radio) radio->frequency = frequency;
}

void rg_radio_set_preset(RgRadio* radio, FuriHalSubGhzPreset preset) {
    if(radio) radio->preset = preset;
}

uint32_t rg_radio_get_frequency(const RgRadio* radio) {
    return radio ? radio->frequency : 0;
}

FuriHalSubGhzPreset rg_radio_get_preset(const RgRadio* radio) {
    return radio ? radio->preset : FuriHalSubGhzPresetOok650Async;
}

void rg_radio_set_decode_callback(RgRadio* radio, RgRadioDecodeCallback cb, void* context) {
    if(!radio) return;
    radio->decode_cb = cb;
    radio->decode_ctx = context;
}

/* ---- receive ------------------------------------------------------------- */

bool rg_radio_start_rx(RgRadio* radio) {
    if(!radio || !radio->connected || !radio->device) return false;
    if(radio->state != RgRadioStateIdle) return false;
    if(!rg_radio_is_frequency_valid(radio, radio->frequency)) {
        FURI_LOG_E(TAG, "Invalid RX frequency %lu", radio->frequency);
        return false;
    }

    subghz_receiver_reset(radio->receiver);
    subghz_worker_start(radio->worker);

    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_load_preset(radio->device, radio->preset, NULL);
    radio->frequency = subghz_devices_set_frequency(radio->device, radio->frequency);
    subghz_devices_set_rx(radio->device);
    subghz_devices_start_async_rx(radio->device, subghz_worker_rx_callback, radio->worker);

    radio->state = RgRadioStateRx;
    return true;
}

void rg_radio_stop_rx(RgRadio* radio) {
    if(!radio || radio->state != RgRadioStateRx) return;

    if(radio->recording) rg_radio_record_stop(radio);

    subghz_devices_stop_async_rx(radio->device);
    if(subghz_worker_is_running(radio->worker)) subghz_worker_stop(radio->worker);
    subghz_devices_idle(radio->device);
    radio->state = RgRadioStateIdle;
}

float rg_radio_get_rssi(RgRadio* radio) {
    if(!radio || radio->state != RgRadioStateRx || !radio->device) return -127.0f;
    return subghz_devices_get_rssi(radio->device);
}

/* ---- RAW recording ------------------------------------------------------- */

bool rg_radio_record_start(RgRadio* radio, const char* name) {
    if(!radio || radio->state != RgRadioStateRx || radio->recording) return false;
    if(!name || name[0] == '\0') return false;

    SubGhzRadioPreset preset = {0};
    preset.frequency = radio->frequency;
    preset.name = furi_string_alloc_set(rg_radio_preset_code(radio->preset));
    preset.data = NULL;
    preset.data_size = 0;

    bool ok = subghz_protocol_raw_save_to_file_init(radio->raw_decoder, name, &preset);
    furi_string_free(preset.name);

    radio->recording = ok;
    if(!ok) FURI_LOG_E(TAG, "Failed to open RAW file for %s", name);
    return ok;
}

void rg_radio_record_stop(RgRadio* radio) {
    if(!radio || !radio->recording) return;
    subghz_protocol_raw_save_to_file_stop(radio->raw_decoder);
    radio->recording = false;
}

bool rg_radio_is_recording(const RgRadio* radio) {
    return radio && radio->recording;
}

size_t rg_radio_record_sample_count(RgRadio* radio) {
    if(!radio || !radio->raw_decoder) return 0;
    return subghz_protocol_raw_get_sample_write(radio->raw_decoder);
}

/* ---- replay (TX) --------------------------------------------------------- */

bool rg_radio_replay_file_start(RgRadio* radio, const char* full_path, FuriString* error) {
    if(!radio || !radio->connected || !radio->device) {
        if(error) furi_string_set(error, "Radio not ready");
        return false;
    }
    if(radio->state != RgRadioStateIdle) {
        if(error) furi_string_set(error, "Radio busy");
        return false;
    }

    /* Read frequency + preset from the file header so we transmit exactly as
     * captured. The RAW sample data itself is streamed by the file encoder. */
    uint32_t frequency = 0;
    FuriHalSubGhzPreset preset = FuriHalSubGhzPresetOok650Async;
    bool header_ok = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* tmp = furi_string_alloc();
    do {
        if(!flipper_format_file_open_existing(ff, full_path)) {
            if(error) furi_string_set(error, "Cannot open file");
            break;
        }
        if(!flipper_format_read_header(ff, tmp, &(uint32_t){0})) {
            if(error) furi_string_set(error, "Bad file header");
            break;
        }
        if(furi_string_cmp_str(tmp, SUBGHZ_RAW_FILE_TYPE) != 0) {
            if(error) furi_string_set(error, "Not a RAW recording");
            break;
        }
        if(!flipper_format_read_uint32(ff, "Frequency", &frequency, 1)) {
            if(error) furi_string_set(error, "No frequency in file");
            break;
        }
        if(!flipper_format_read_string(ff, "Preset", tmp)) {
            if(error) furi_string_set(error, "No preset in file");
            break;
        }
        if(!rg_radio_preset_from_file_name(furi_string_get_cstr(tmp), &preset)) {
            if(error) furi_string_set(error, "Unsupported preset");
            break;
        }
        header_ok = true;
    } while(false);
    furi_string_free(tmp);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);

    if(!header_ok) return false;

    if(!rg_radio_is_frequency_valid(radio, frequency)) {
        if(error) furi_string_set(error, "Invalid frequency");
        return false;
    }
    if(!rg_radio_is_tx_allowed(radio, frequency)) {
        if(error) furi_string_set(error, "TX blocked by region");
        return false;
    }

    radio->tx_worker = subghz_file_encoder_worker_alloc();
    if(!subghz_file_encoder_worker_start(
           radio->tx_worker, full_path, subghz_devices_get_name(radio->device))) {
        if(error) furi_string_set(error, "Cannot load samples");
        subghz_file_encoder_worker_free(radio->tx_worker);
        radio->tx_worker = NULL;
        return false;
    }
    /* Let the worker buffer a few samples before the DMA starts pulling. */
    furi_delay_ms(100);

    radio->frequency = frequency;
    radio->preset = preset;
    subghz_devices_reset(radio->device);
    subghz_devices_idle(radio->device);
    subghz_devices_load_preset(radio->device, preset, NULL);
    subghz_devices_set_frequency(radio->device, frequency);

    if(!subghz_devices_set_tx(radio->device)) {
        if(error) furi_string_set(error, "TX not allowed");
        subghz_file_encoder_worker_stop(radio->tx_worker);
        subghz_file_encoder_worker_free(radio->tx_worker);
        radio->tx_worker = NULL;
        subghz_devices_idle(radio->device);
        return false;
    }

    subghz_devices_start_async_tx(
        radio->device, subghz_file_encoder_worker_get_level_duration, radio->tx_worker);
    radio->state = RgRadioStateTx;
    return true;
}

bool rg_radio_tx_is_running(RgRadio* radio) {
    if(!radio || radio->state != RgRadioStateTx || !radio->device) return false;
    /* The async TX completes once the encoder has yielded its whole upload
     * (the file worker drains, then the final buffer finishes on the air). */
    return !subghz_devices_is_async_complete_tx(radio->device);
}

void rg_radio_stop_tx(RgRadio* radio) {
    if(!radio || radio->state != RgRadioStateTx) return;

    subghz_devices_stop_async_tx(radio->device);
    if(radio->tx_worker) {
        subghz_file_encoder_worker_stop(radio->tx_worker);
        subghz_file_encoder_worker_free(radio->tx_worker);
        radio->tx_worker = NULL;
    }
    subghz_devices_idle(radio->device);
    radio->state = RgRadioStateIdle;
}
