/**
 * @file rg_glyph.h
 * @brief A 3x5 pixel font for dense bit and hex rows on the 128x64 screen.
 *
 * The Flipper's smallest text font is about 5 pixels wide, which fits 21-25
 * characters a line. Bit and hex dumps need more: with 3x5 glyphs on a 4
 * pixel pitch a line holds 24 bits in three 8-bit groups plus a position
 * label. Covers 0-9, A-F, '.', 'X', '?', '-', ':', '+' and space; anything
 * else draws as '?' so a missing glyph is never shown as blank.
 *
 * Pure C with no SDK includes; host-tested (test/test_ui.c). The view draws
 * a glyph by asking which of its 15 pixels are set.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RG_GLYPH_W     3
#define RG_GLYPH_H     5
#define RG_GLYPH_PITCH 4 // glyph + 1 pixel space

/** Rows of a glyph, top first; bit 2 is the leftmost pixel of a row. */
const uint8_t* rg_glyph_rows(char c);

/** Whether pixel (@p x, @p y) of glyph @p c is set (0 <= x < 3, 0 <= y < 5). */
bool rg_glyph_pixel(char c, uint8_t x, uint8_t y);

/** Width in pixels of @p n glyphs at the normal pitch (no trailing space). */
uint16_t rg_glyph_text_width(uint16_t n);
