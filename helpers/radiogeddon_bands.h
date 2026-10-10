/**
 * @file radiogeddon_bands.h
 * @brief The modulation presets and frequency lists the app offers.
 *
 * Kept apart from radiogeddon_subghz.h, which needs the whole radio SDK, so
 * that the settings code and its host tests (test/test_formats.c) can use
 * the same tables.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <lib/subghz/devices/preset.h>

#define RADIOGEDDON_FREQUENCY_DEFAULT 433920000UL

/** A selectable modulation preset with a stable, file-compatible name. */
typedef struct {
    const char* label; // shown in the UI
    const char* file_name; // written into .sub "Preset" field (firmware-compatible)
    // The firmware's own name for it ("AM650"), which a decoder is given when
    // it saves a key: the firmware turns it into file_name, and writes any name
    // it does not know as a custom preset with the register list it was given.
    const char* setting_name;
    FuriHalSubGhzPreset preset;
} RadioGeddonPreset;

extern const RadioGeddonPreset radiogeddon_presets[];
extern const size_t radiogeddon_presets_count;

/** Index of the preset a .sub file's "Preset" value names, or -1. */
int32_t radiogeddon_preset_find_file_name(const char* file_name);

/** Candidate frequencies (Hz) scanned by the analyzer. Region-filtered at use. */
extern const uint32_t radiogeddon_frequencies[];
extern const size_t radiogeddon_frequencies_count;

/** Smaller frequency set cycled by the Frequency Hopper. */
extern const uint32_t radiogeddon_hopper_frequencies[];
extern const size_t radiogeddon_hopper_frequencies_count;
