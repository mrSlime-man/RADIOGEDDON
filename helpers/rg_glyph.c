#include "rg_glyph.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_BITSTREAM || RG_FEATURE_MULTI_COMPARE

typedef struct {
    char c;
    uint8_t rows[RG_GLYPH_H];
} RgGlyph;

static const RgGlyph rg_glyphs[] = {
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 3, 1, 7}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}}, {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}}, {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'.', {0, 0, 0, 0, 2}}, {'X', {5, 5, 2, 5, 5}}, {'?', {6, 1, 2, 0, 2}}, {'-', {0, 0, 7, 0, 0}},
    {':', {0, 2, 0, 2, 0}}, {'+', {0, 2, 7, 2, 0}}, {' ', {0, 0, 0, 0, 0}},
};

const uint8_t* rg_glyph_rows(char c) {
    if(c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    if(c == 'x') c = 'X';
    for(unsigned i = 0; i < sizeof(rg_glyphs) / sizeof(rg_glyphs[0]); i++)
        if(rg_glyphs[i].c == c) return rg_glyphs[i].rows;
    return rg_glyph_rows('?');
}

bool rg_glyph_pixel(char c, uint8_t x, uint8_t y) {
    if(x >= RG_GLYPH_W || y >= RG_GLYPH_H) return false;
    return (rg_glyph_rows(c)[y] >> (RG_GLYPH_W - 1 - x)) & 1u;
}

uint16_t rg_glyph_text_width(uint16_t n) {
    return n ? (uint16_t)(n * RG_GLYPH_PITCH - 1u) : 0;
}

#endif
