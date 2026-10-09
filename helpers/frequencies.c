#include "frequencies.h"

/*
 * The default CC1101 tuning plan used by the Flipper Sub-GHz stack. These
 * cover the 300-348, 387-464 and 779-928 MHz bands that the radio supports.
 * The list mirrors the firmware's default frequency set so behaviour is
 * familiar to existing Sub-GHz users.
 */
const RgFrequency rg_frequencies[] = {
    {300000000, "300.00"},
    {303875000, "303.87"},
    {304250000, "304.25"},
    {310000000, "310.00"},
    {315000000, "315.00"},
    {318000000, "318.00"},
    {390000000, "390.00"},
    {418000000, "418.00"},
    {433075000, "433.07"},
    {433420000, "433.42"},
    {433920000, "433.92"},
    {434420000, "434.42"},
    {434775000, "434.77"},
    {438900000, "438.90"},
    {868350000, "868.35"},
    {915000000, "915.00"},
    {925000000, "925.00"},
};

const size_t rg_frequencies_count = sizeof(rg_frequencies) / sizeof(rg_frequencies[0]);

/* 433.92 MHz - the most common ISM frequency for remotes. */
const size_t rg_frequencies_default_index = 10;

const uint32_t rg_hopper_frequencies[] = {
    315000000,
    433920000,
    868350000,
    915000000,
};

const size_t rg_hopper_frequencies_count =
    sizeof(rg_hopper_frequencies) / sizeof(rg_hopper_frequencies[0]);
