/* The frame lengths the firmware's own decoders decode from each of its RAW
 * test captures (lib/subghz on applications/.../unit_tests/subghz, pinned
 * release). test_fwdecode checks this table against what the decoders
 * decode on every run, so it cannot drift; test_fwanalyze scores the
 * analyzer's frame length against it as well as against each decoder's
 * min_count_bit_for_found (a decoder's minimum is not always what was sent:
 * Nice FLO accepts 12 to 24 bits and this capture is 24).
 *
 * Captures whose decodes cannot be read back as a key (no Bit line) have no
 * entry. Generated from `make decoders` output; edit only with it. */
#pragma once

typedef struct {
    const char* file;
    unsigned bits[2]; /* the lengths decoded (0: none) */
} FwDecodedBits;

static const FwDecodedBits fw_decoded_bits[] = {
    {"came_raw.sub", {12, 0}},
    {"came_twee_raw.sub", {54, 0}},
    {"faac_slh_raw.sub", {64, 0}},
    {"gate_tx_raw.sub", {24, 0}},
    {"hormann_hsm_raw.sub", {44, 0}},
    {"ido_117_111_raw.sub", {48, 0}},
    {"doorhan_raw.sub", {64, 0}},
    {"kia_seed_raw.sub", {61, 0}},
    {"nero_radio_raw.sub", {56, 0}},
    {"nero_sketch_raw.sub", {40, 0}},
    {"nice_flo_raw.sub", {24, 20}},
    {"nice_flor_s_raw.sub", {52, 0}},
    {"Princeton_raw.sub", {24, 0}},
    {"scher_khan_magic_code.sub", {51, 0}},
    {"Somfy_keytis_raw.sub", {80, 0}},
    {"somfy_telis_raw.sub", {56, 0}},
    {"cenmax_raw.sub", {64, 0}},
    {"linear_raw.sub", {10, 0}},
    {"linear_delta3_raw.sub", {8, 0}},
    {"megacode_raw.sub", {24, 0}},
    {"security_pls_1_0_raw.sub", {42, 0}},
    {"security_pls_2_0_raw.sub", {62, 0}},
    {"holtek_raw.sub", {40, 0}},
    {"power_smart_raw.sub", {64, 0}},
    {"marantec_raw.sub", {49, 0}},
    {"bett_raw.sub", {18, 0}},
    {"doitrand_raw.sub", {37, 0}},
    {"phoenix_v2_raw.sub", {52, 0}},
    {"honeywell_wdb_raw.sub", {48, 0}},
    {"magellan_raw.sub", {32, 0}},
    {"intertechno_v3_raw.sub", {32, 0}},
    {"clemsa_raw.sub", {18, 0}},
    {"ansonic_raw.sub", {12, 0}},
    {"smc5326_raw.sub", {25, 0}},
    {"holtek_ht12x_raw.sub", {12, 0}},
    {"dooya_raw.sub", {40, 0}},
    {"nice_one_raw.sub", {72, 0}},
    {"kinggates_stylo4k_raw.sub", {89, 0}},
    {"mastercode_raw.sub", {36, 0}},
    {"dickert_raw.sub", {36, 0}},
    {"roger_raw.sub", {28, 0}},
    {"hollarm_raw.sub", {42, 0}},
    {"revers_rb2_raw.sub", {64, 0}},
    {"gangqi_raw.sub", {34, 0}},
    {"hay21_raw.sub", {21, 0}},
    {"feron_raw.sub", {32, 0}},
    {"legrand_raw.sub", {18, 0}},
    {"marantec24_raw.sub", {24, 0}},
    {"elplast_raw.sub", {18, 0}},
};
