/**
 * @file radiogeddon_subghz.h
 * @brief Thin wrapper around the firmware Sub-GHz subsystem.
 *
 * Owns the radio device, the protocol environment/receiver (for live decoding
 * of known protocols) and the RAW decoder (for capturing unknown transmissions
 * to a .sub file). All radio access goes through the public subghz_devices_*
 * API so the same code works with the internal CC1101 and, where present,
 * supported external modules.
 */
#pragma once

#include <furi.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/preset.h>
#include <lib/subghz/protocols/raw.h>

#define RADIOGEDDON_FREQUENCY_DEFAULT 433920000UL

/** A selectable modulation preset with a stable, file-compatible name. */
typedef struct {
    const char* label; // shown in the UI
    const char* file_name; // written into .sub "Preset" field (firmware-compatible)
    FuriHalSubGhzPreset preset;
} RadioGeddonPreset;

extern const RadioGeddonPreset radiogeddon_presets[];
extern const size_t radiogeddon_presets_count;

/** Candidate frequencies (Hz) scanned by the analyzer. Region-filtered at use. */
extern const uint32_t radiogeddon_frequencies[];
extern const size_t radiogeddon_frequencies_count;

/** Smaller frequency set cycled by the Frequency Hopper. */
extern const uint32_t radiogeddon_hopper_frequencies[];
extern const size_t radiogeddon_hopper_frequencies_count;

typedef struct RadioGeddonSubGhz RadioGeddonSubGhz;

/**
 * Fired when a known protocol is successfully decoded.
 * @param protocol_name  decoder name (e.g. "Princeton")
 * @param hash           rolling hash of the parcel (de-dup aid)
 * @param text           multi-line human-readable description
 * @param serialized     complete .sub file content, ready to persist verbatim
 * @param context        user context
 */
typedef void (*RadioGeddonSubGhzDecodeCallback)(
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized,
    void* context);

RadioGeddonSubGhz* radiogeddon_subghz_alloc(void);
void radiogeddon_subghz_free(RadioGeddonSubGhz* instance);

/** True if a usable radio device is present and responding. */
bool radiogeddon_subghz_is_device_present(RadioGeddonSubGhz* instance);

/** Human-readable name of the active radio device (e.g. "cc1101_int"). */
const char* radiogeddon_subghz_device_name(RadioGeddonSubGhz* instance);

void radiogeddon_subghz_set_frequency(RadioGeddonSubGhz* instance, uint32_t frequency);
uint32_t radiogeddon_subghz_get_frequency(RadioGeddonSubGhz* instance);
void radiogeddon_subghz_set_preset(RadioGeddonSubGhz* instance, uint8_t preset_index);

/**
 * True if the frequency is valid for the radio hardware. This does NOT check
 * the region policy — regional transmit permission is enforced separately by
 * subghz_devices_set_tx() at transmit time.
 */
bool radiogeddon_subghz_is_frequency_allowed(RadioGeddonSubGhz* instance, uint32_t frequency);

/**
 * Begin receiving. When @p decode_callback is non-NULL the configured protocol
 * decoders run and fire the callback on each successful decode. The call also
 * starts live RSSI sampling (see radiogeddon_subghz_get_rssi).
 */
void radiogeddon_subghz_rx_start(
    RadioGeddonSubGhz* instance,
    RadioGeddonSubGhzDecodeCallback decode_callback,
    void* context);

void radiogeddon_subghz_rx_stop(RadioGeddonSubGhz* instance);
bool radiogeddon_subghz_is_rx_running(RadioGeddonSubGhz* instance);

/**
 * Retune an already-running RX session to a new frequency without powering the
 * radio down (used by the Frequency Hopper). The decode callback stays active.
 * Invalid (out-of-band) frequencies are ignored. No-op if RX is not running.
 */
void radiogeddon_subghz_rx_retune(RadioGeddonSubGhz* instance, uint32_t frequency);

/** Instantaneous RSSI in dBm for the current frequency (valid while RX runs). */
float radiogeddon_subghz_get_rssi(RadioGeddonSubGhz* instance);

/**
 * Open a lightweight scanning session (radio powered, no async RX). While a
 * session is open, radiogeddon_subghz_probe_rssi() only retunes and reads,
 * which is fast enough to sweep many frequencies per second.
 */
void radiogeddon_subghz_scan_begin(RadioGeddonSubGhz* instance);
void radiogeddon_subghz_scan_end(RadioGeddonSubGhz* instance);

/** Quick synchronous RSSI probe of a frequency (used by the scanner sweep). */
float radiogeddon_subghz_probe_rssi(RadioGeddonSubGhz* instance, uint32_t frequency);

/**
 * Like radiogeddon_subghz_probe_rssi(), but keeps sampling for @p dwell_ms
 * after the AGC settles and returns the strongest reading. Blocks the caller
 * for about 3 ms + dwell_ms.
 */
float radiogeddon_subghz_probe_rssi_dwell(
    RadioGeddonSubGhz* instance,
    uint32_t frequency,
    uint32_t dwell_ms);

/**
 * Start capturing the incoming raw timing stream to @p file_path (.sub RAW
 * format). Must be called while RX is running. Returns false on I/O failure.
 */
bool radiogeddon_subghz_record_start(RadioGeddonSubGhz* instance, const char* file_path);

/** Stop an in-progress RAW capture and flush the file. */
void radiogeddon_subghz_record_stop(RadioGeddonSubGhz* instance);
bool radiogeddon_subghz_is_recording(RadioGeddonSubGhz* instance);

/** Number of RAW samples written so far in the active capture. */
size_t radiogeddon_subghz_record_sample_count(RadioGeddonSubGhz* instance);

/** True if the RAW capture buffer filled up (capture truncated). */
bool radiogeddon_subghz_record_overflowed(RadioGeddonSubGhz* instance);

/**
 * Write the captured RAW buffer to @p file_path as a Flipper RAW .sub file.
 * Call after radiogeddon_subghz_record_stop(). Returns false if nothing was
 * captured or on I/O failure.
 */
bool radiogeddon_subghz_record_flush_to_file(RadioGeddonSubGhz* instance, const char* file_path);

/* ---- Replay / transmit -------------------------------------------------- */

/**
 * Transmit a previously saved .sub file once. The firmware enforces regional
 * transmit restrictions; this returns false if transmission is not permitted,
 * the file is unsupported, or no radio is present. Protected (rolling-code)
 * protocols are intentionally not transmitted by this toolkit.
 */
typedef enum {
    RadioGeddonTxOk,
    RadioGeddonTxErrorNoFile,
    RadioGeddonTxErrorParse,
    RadioGeddonTxErrorRegion,
    RadioGeddonTxErrorProtected,
    RadioGeddonTxErrorNoDevice,
    RadioGeddonTxErrorBusy,
} RadioGeddonTxResult;

typedef void (*RadioGeddonTxCompleteCallback)(void* context);

RadioGeddonTxResult radiogeddon_subghz_tx_start(
    RadioGeddonSubGhz* instance,
    const char* file_path,
    RadioGeddonTxCompleteCallback complete_cb,
    void* context);

void radiogeddon_subghz_tx_stop(RadioGeddonSubGhz* instance);
bool radiogeddon_subghz_is_tx_running(RadioGeddonSubGhz* instance);
