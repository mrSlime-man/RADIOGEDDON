/**
 * @file rg_freq.h
 * @brief Frequency text and range checks shared by the screens and settings.
 *
 * Pure C with no SDK includes, so it is unit-tested on the host
 * (test/test_freq.c). Which frequencies the radio can really tune is the
 * firmware's call (radiogeddon_subghz_is_frequency_allowed); the range here
 * only rejects values no supported firmware tunes, such as a damaged
 * settings file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The widest range a supported firmware lets the CC1101 tune: Unleashed's
 * 281-361, 378-481 and 749-962 MHz. Official allows 300-348, 387-464 and
 * 779-928 MHz. */
#define RG_FREQ_MIN_HZ 281000000UL
#define RG_FREQ_MAX_HZ 962000000UL

/* Custom frequencies are entered in kHz. */
#define RG_FREQ_MIN_KHZ (RG_FREQ_MIN_HZ / 1000UL)
#define RG_FREQ_MAX_KHZ (RG_FREQ_MAX_HZ / 1000UL)

/* Longest text rg_freq_text writes, with its NUL ("962.000"). */
#define RG_FREQ_TEXT_SIZE 12

/**
 * Frequency in MHz: two decimals for whole 10 kHz steps ("433.92"), three
 * otherwise ("433.075"). Digits below 1 kHz are dropped.
 */
void rg_freq_text(uint32_t hz, char* out, size_t out_size);

/** True if @p hz is within RG_FREQ_MIN_HZ..RG_FREQ_MAX_HZ. Not a radio or region check. */
bool rg_freq_in_range(uint32_t hz);

/** Index of the entry of @p list nearest to @p hz (the first on a tie); 0 if @p n is 0. */
size_t rg_freq_nearest(const uint32_t* list, size_t n, uint32_t hz);

/** Index of @p hz in @p list, or @p n when it is not there. */
size_t rg_freq_find(const uint32_t* list, size_t n, uint32_t hz);
