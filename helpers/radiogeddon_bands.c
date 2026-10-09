#include "radiogeddon_bands.h"

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
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000, 390000000,
    418000000, 433075000, 433420000, 433920000, 434420000, 434775000, 438900000,
    464000000, 779000000, 868350000, 915000000, 925000000,
};
const size_t radiogeddon_frequencies_count =
    sizeof(radiogeddon_frequencies) / sizeof(radiogeddon_frequencies[0]);

// The Frequency Hopper cycles a short list of the most common OOK bands so each
// dwell is long enough to catch and decode a transmission.
const uint32_t radiogeddon_hopper_frequencies[] = {
    315000000,
    390000000,
    433920000,
    868350000,
};
const size_t radiogeddon_hopper_frequencies_count =
    sizeof(radiogeddon_hopper_frequencies) / sizeof(radiogeddon_hopper_frequencies[0]);
