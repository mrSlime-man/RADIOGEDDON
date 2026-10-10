/**
 * @file radiogeddon_decode_text.h
 * @brief The description shown for a decoded signal, and the preset its key
 * is saved with.
 *
 * Usually the decoder's own text. RadioGeddon gives the decoders no rainbow
 * tables (the files some firmware decoders use to undo a rolling code's
 * obfuscation), and the Official firmware's CAME Atomo and Alutech AT-4N
 * decoders read their table's file name without checking it when they
 * describe a decode: with none set, that is a NULL dereference and the
 * Flipper crashes. For those two, the description is built from the decoded
 * data instead: name, bit count and key.
 */
#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>

/**
 * Describe @p decoder_base's last decode into @p out (replaced). @p preset is
 * used only to serialize the decode for the two decoders above; NULL is
 * allowed. Returns whether there is a description.
 */
bool radiogeddon_decode_text(
    SubGhzProtocolDecoderBase* decoder_base,
    SubGhzRadioPreset* preset,
    FuriString* out);

/** Whether the decoder of @p protocol_name gets the data-only description. */
bool radiogeddon_decode_text_avoids_decoder(const char* protocol_name);

/**
 * The preset a decode received on radiogeddon_presets[@p index] is saved
 * with: the firmware's short name for it ("AM650"), which the decoder writes
 * as the file's Preset ("FuriHalSubGhzPresetOok650Async"). The caller frees
 * preset->name.
 */
void radiogeddon_decode_preset(SubGhzRadioPreset* preset, size_t index, uint32_t frequency);
