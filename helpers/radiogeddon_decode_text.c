#include "radiogeddon_decode_text.h"
#include "radiogeddon_bands.h"

#include <lib/flipper_format/flipper_format.h>
#include <string.h>

/* Their names in the firmware (SUBGHZ_PROTOCOL_*_NAME, not in the SDK). */
static const char* const radiogeddon_table_decoders[] = {
    "CAME Atomo",
    "Alutech AT-4N",
};

bool radiogeddon_decode_text_avoids_decoder(const char* protocol_name) {
    if(!protocol_name) return false;
    for(size_t i = 0; i < COUNT_OF(radiogeddon_table_decoders); i++) {
        if(strcmp(protocol_name, radiogeddon_table_decoders[i]) == 0) return true;
    }
    return false;
}

/* Name, bit count and key from the decode's serialized form. */
static bool radiogeddon_decode_text_from_data(
    SubGhzProtocolDecoderBase* decoder_base,
    SubGhzRadioPreset* preset,
    FuriString* out) {
    SubGhzRadioPreset blank = {.frequency = 0, .name = NULL, .data = NULL, .data_size = 0};
    if(!preset) {
        blank.name = furi_string_alloc_set("AM650");
        preset = &blank;
    }
    FlipperFormat* ff = flipper_format_string_alloc();
    uint32_t bits = 0;
    uint8_t key[8] = {0};
    bool ok = subghz_protocol_decoder_base_serialize(decoder_base, ff, preset) ==
                  SubGhzProtocolStatusOk &&
              flipper_format_rewind(ff) && flipper_format_read_uint32(ff, "Bit", &bits, 1) &&
              flipper_format_read_hex(ff, "Key", key, sizeof(key));
    flipper_format_free(ff);
    if(blank.name) furi_string_free(blank.name);

    furi_string_printf(out, "%s", decoder_base->protocol->name);
    if(ok) {
        furi_string_cat_printf(out, " %lubit\r\nKey:", (unsigned long)bits);
        for(size_t i = 0; i < sizeof(key); i++) {
            furi_string_cat_printf(out, "%02X", key[i]);
        }
    }
    furi_string_cat_str(
        out, "\r\nSerial, button and\r\ncounter need a rainbow\r\ntable (not used).\r\n");
    return true;
}

bool radiogeddon_decode_text(
    SubGhzProtocolDecoderBase* decoder_base,
    SubGhzRadioPreset* preset,
    FuriString* out) {
    furi_string_reset(out);
    const char* name = decoder_base->protocol ? decoder_base->protocol->name : NULL;
    if(radiogeddon_decode_text_avoids_decoder(name)) {
        return radiogeddon_decode_text_from_data(decoder_base, preset, out);
    }
    return subghz_protocol_decoder_base_get_string(decoder_base, out);
}

void radiogeddon_decode_preset(SubGhzRadioPreset* preset, size_t index, uint32_t frequency) {
    if(index >= radiogeddon_presets_count) index = 0;
    // Not the file name: the firmware writes a name it does not know as a
    // custom preset with the register list given here (none), and neither
    // the stock app nor Replay can load that key.
    preset->name = furi_string_alloc_set(radiogeddon_presets[index].setting_name);
    preset->frequency = frequency;
    preset->data = NULL;
    preset->data_size = 0;
}
