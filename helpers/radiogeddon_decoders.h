/**
 * @file radiogeddon_decoders.h
 * @brief A receiver holding only the decoders it will feed.
 *
 * The firmware's subghz_receiver_alloc_init() allocates a decoder for every
 * protocol of the environment's registry, but subghz_receiver_decode() feeds
 * a decoder only when its protocol's flags match the receiver's filter
 * (lib/subghz/receiver.c, the same in the five pinned firmware trees). The
 * app always filters to SubGhzProtocolFlag_Decodable, which two registered
 * protocols do not carry: RAW and BinRAW. BinRAW's decoder alone allocates
 * two capture buffers, 2,048 x 4 B and 2,048 B (lib/subghz/protocols/
 * bin_raw.c, BIN_RAW_BUF_RAW_SIZE in all five trees), that it can never use
 * under that filter.
 *
 * radiogeddon_decoders_receiver_alloc() lends the environment a registry of
 * just the protocols the filter feeds, in the firmware's own order, while the
 * receiver allocates its decoders, then gives the full registry back (the
 * transmitter and the protocol lookups by name keep seeing every protocol).
 * The receiver then feeds exactly the decoders, in exactly the order, that a
 * receiver of the full registry would feed, so every decode is the same
 * (test/test_fwdecode.c checks it on the firmware's own captures).
 */
#pragma once

#include <furi.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/types.h>

/* The heap BinRAW's decoder takes besides its object: its two capture
 * buffers (2,048 int32 levels and 2,048 bytes), freed by not allocating it. */
#define RADIOGEDDON_DECODERS_BINRAW_BUFFERS (2048u * 4u + 2048u)

/**
 * Whether a receiver filtered to @p filter feeds @p protocol's decoder: the
 * test subghz_receiver_decode() makes (no ignore filter is ever set here).
 */
bool radiogeddon_decoders_fed(const SubGhzProtocol* protocol, SubGhzProtocolFlag filter);

/**
 * The protocols of @p all whose decoders a receiver filtered to @p filter
 * feeds, in registry order, written to @p out (room for @p cap). Returns how
 * many there are (more than @p cap means @p out holds only the first @p cap).
 */
size_t radiogeddon_decoders_select(
    const SubGhzProtocolRegistry* all,
    SubGhzProtocolFlag filter,
    const SubGhzProtocol** out,
    size_t cap);

/**
 * Whether @p all has a BinRAW protocol a receiver filtered to @p filter never
 * feeds: a receiver of the full registry then allocated its buffers.
 */
bool radiogeddon_decoders_skips_binraw(
    const SubGhzProtocolRegistry* all,
    SubGhzProtocolFlag filter);

/**
 * A receiver of @p environment (whose registry is @p all) with only the
 * decoders a @p filter receiver feeds, its filter set to @p filter. The
 * environment's registry is @p all again on return.
 */
SubGhzReceiver* radiogeddon_decoders_receiver_alloc(
    SubGhzEnvironment* environment,
    const SubGhzProtocolRegistry* all,
    SubGhzProtocolFlag filter);
