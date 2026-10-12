/**
 * @file rg_crc.h
 * @brief CRC-32 (IEEE 802.3, reflected, as zlib) for RadioGeddon's own small
 * binary files: the fingerprint index and the research journal. Pure C,
 * bitwise (no table: 0 bytes of RAM, these files are a few kilobytes).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** Continue a CRC-32 over @p n bytes (start with 0). */
uint32_t rg_crc32(uint32_t crc, const uint8_t* data, size_t n);
