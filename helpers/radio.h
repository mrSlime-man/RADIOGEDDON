#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/devices/preset.h>
#include <lib/subghz/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RadioGeddon radio abstraction.
 *
 * Wraps the firmware's portable `subghz_devices` layer (the same API used by
 * the stock Sub-GHz app) so the toolkit runs unmodified on official firmware
 * and on Unleashed/RogueMaster, which all expose this layer. The wrapper adds:
 *   - explicit, queryable state (so the UI never issues an illegal op);
 *   - defensive init that reports a missing/disconnected radio instead of
 *     dereferencing NULL;
 *   - combined live protocol decoding + optional RAW-to-file recording;
 *   - region-checked transmit for authorized replay.
 *
 * All functions are safe to call with a NULL instance (they no-op / return a
 * safe default), which keeps error paths in the scenes simple.
 */

typedef struct RgRadio RgRadio;

typedef enum {
    RgRadioStateIdle, /**< powered, not receiving or transmitting */
    RgRadioStateRx, /**< async RX running */
    RgRadioStateTx, /**< async TX (replay) running */
} RgRadioState;

/** Modulation preset the user can choose. */
typedef struct {
    FuriHalSubGhzPreset preset;
    const char* code; /**< short code written to .sub files, e.g. "AM650" */
    const char* label; /**< UI label, e.g. "AM650 OOK" */
} RgRadioPresetInfo;

extern const RgRadioPresetInfo rg_radio_presets[];
extern const size_t rg_radio_presets_count;
extern const size_t rg_radio_preset_default_index;

/**
 * Called from a worker thread whenever a known protocol is decoded.
 * `decoded_text` is owned by the caller and only valid for the duration of
 * the call - copy what you need.
 */
typedef void (*RgRadioDecodeCallback)(
    void* context,
    const char* protocol_name,
    SubGhzProtocolType type,
    FuriString* decoded_text);

RgRadio* rg_radio_alloc(void);
void rg_radio_free(RgRadio* radio);

/**
 * Power up and probe the internal radio.
 * @return true if a radio was found and reports connected.
 * On failure the instance stays usable but is_connected() returns false and
 * RX/TX are refused, so the UI can show an error rather than crash.
 */
bool rg_radio_begin(RgRadio* radio);

/** Power down the radio and release the device. Safe to call repeatedly. */
void rg_radio_end(RgRadio* radio);

bool rg_radio_is_connected(const RgRadio* radio);
RgRadioState rg_radio_get_state(const RgRadio* radio);

/** True if the CC1101 can tune this frequency at all. */
bool rg_radio_is_frequency_valid(const RgRadio* radio, uint32_t frequency);

/** True if transmit is permitted here by the firmware's region policy. */
bool rg_radio_is_tx_allowed(const RgRadio* radio, uint32_t frequency);

/** Select frequency/preset to be used by the next RX/TX start. */
void rg_radio_set_frequency(RgRadio* radio, uint32_t frequency);
void rg_radio_set_preset(RgRadio* radio, FuriHalSubGhzPreset preset);
uint32_t rg_radio_get_frequency(const RgRadio* radio);
FuriHalSubGhzPreset rg_radio_get_preset(const RgRadio* radio);

/** Register the decode callback invoked on each successfully decoded signal. */
void rg_radio_set_decode_callback(RgRadio* radio, RgRadioDecodeCallback cb, void* context);

/**
 * Start receiving on the currently selected frequency/preset. Live protocol
 * decoding runs automatically; use rg_radio_record_start() to also capture
 * RAW samples to a file. Idempotent-safe: refuses if not connected or already
 * busy and returns false.
 */
bool rg_radio_start_rx(RgRadio* radio);
void rg_radio_stop_rx(RgRadio* radio);

/** Instantaneous RSSI in dBm (valid only while receiving). */
float rg_radio_get_rssi(RgRadio* radio);

/**
 * Begin writing received RAW samples to `full_path` (a .sub file). Must be
 * called while RX is running. The chosen frequency/preset are written into
 * the file header so the recording can be replayed/loaded later.
 * @return true on success.
 */
bool rg_radio_record_start(RgRadio* radio, const char* full_path);
void rg_radio_record_stop(RgRadio* radio);
bool rg_radio_is_recording(const RgRadio* radio);
size_t rg_radio_record_sample_count(RgRadio* radio);

/**
 * Replay a previously captured RAW .sub file. Reads the frequency and preset
 * from the file, verifies TX is allowed by region, then transmits once.
 * Non-blocking: starts the async TX and returns. Poll rg_radio_tx_is_running()
 * and call rg_radio_stop_tx() when complete (or to abort).
 * @param error optional out: human-readable failure reason.
 * @return true if transmission started.
 */
bool rg_radio_replay_file_start(RgRadio* radio, const char* full_path, FuriString* error);
bool rg_radio_tx_is_running(RgRadio* radio);
void rg_radio_stop_tx(RgRadio* radio);

#ifdef __cplusplus
}
#endif
