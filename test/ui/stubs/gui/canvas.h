/**
 * Host stand-in for the firmware's gui/canvas.h (test/ui): the drawing calls
 * RadioGeddon's views make, drawn into a 128x64 frame buffer by
 * test/ui/canvas_host.c with the firmware's own u8g2 fonts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ColorWhite = 0x00,
    ColorBlack = 0x01,
    ColorXOR = 0x02,
} Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

typedef struct Canvas Canvas;

void canvas_clear(Canvas* canvas);
void canvas_set_color(Canvas* canvas, Color color);
void canvas_invert_color(Canvas* canvas);
void canvas_set_font(Canvas* canvas, Font font);
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void canvas_draw_str_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str);
uint16_t canvas_string_width(Canvas* canvas, const char* str);
void canvas_draw_dot(Canvas* canvas, int32_t x, int32_t y);
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void canvas_draw_xbm(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    const uint8_t* bitmap);
