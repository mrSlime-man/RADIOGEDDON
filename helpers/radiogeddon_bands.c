#include "radiogeddon_bands.h"

#include <string.h>

const RadioGeddonPreset radiogeddon_presets[] = {
    {"AM 270", "FuriHalSubGhzPresetOok270Async", "AM270", FuriHalSubGhzPresetOok270Async},
    {"AM 650", "FuriHalSubGhzPresetOok650Async", "AM650", FuriHalSubGhzPresetOok650Async},
    {"FM 2.38k", "FuriHalSubGhzPreset2FSKDev238Async", "FM238", FuriHalSubGhzPreset2FSKDev238Async},
    {"FM 47.6k", "FuriHalSubGhzPreset2FSKDev476Async", "FM476", FuriHalSubGhzPreset2FSKDev476Async},
};
const size_t radiogeddon_presets_count =
    sizeof(radiogeddon_presets) / sizeof(radiogeddon_presets[0]);

int32_t radiogeddon_preset_find_file_name(const char* file_name) {
    if(!file_name) return -1;
    for(size_t i = 0; i < radiogeddon_presets_count; i++) {
        if(strcmp(file_name, radiogeddon_presets[i].file_name) == 0) return (int32_t)i;
    }
    return -1;
}

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
