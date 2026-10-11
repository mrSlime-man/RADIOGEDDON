/**
 * Host UI harness (test/ui/canvas_host.c): the frame buffer behind the
 * stand-in canvas and view, and what tests read back from it.
 */
#pragma once

#include <gui/view.h>

#define CANVAS_W 128
#define CANVAS_H 64

Canvas* canvas_host(void);
void canvas_host_reset(Canvas* canvas);
/** Pixels drawn outside the screen since the reset. */
uint32_t canvas_host_clipped(const Canvas* canvas);
/** Texts that did not fit on the screen since the reset, and the last one. */
uint32_t canvas_host_text_overflows(const Canvas* canvas);
const char* canvas_host_last_overflow(const Canvas* canvas);
bool canvas_host_pixel(const Canvas* canvas, int x, int y);
/** Set pixels in a rectangle. */
uint32_t canvas_host_count(const Canvas* canvas, int x, int y, int w, int h);
/** Write the screen as a PBM image (a synthetic development preview). */
bool canvas_host_write_pbm(const Canvas* canvas, const char* path);
/** Print the screen as text (debugging). */
void canvas_host_dump(const Canvas* canvas);
