/* Host stand-in for gui/elements.h (test/ui): the elements the views use. */
#pragma once
#include <gui/canvas.h>

/** A scroll bar along the right edge from @p y, @p height pixels tall. */
void elements_scrollbar_pos(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t height,
    size_t pos,
    size_t total);
