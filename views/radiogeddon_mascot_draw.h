/**
 * @file radiogeddon_mascot_draw.h
 * @brief Dolphin Lab (Full edition): drawing the mascot, its props and the
 * small badge the radio screens show in their header.
 *
 * Everything is drawn from the generated sprites (helpers/rg_sprites.h) and
 * a few canvas primitives, straight into the canvas: no frame buffer of its
 * own, nothing allocated. Sprites are drawn with transparent clear bits, so
 * an overlay never erases what is under it.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <gui/canvas.h>
#include "../helpers/rg_mascot.h"
#include "../helpers/rg_sprites.h"

/* The dolphin's box, relative to the point it is drawn at: the antenna
 * reaches RG_MASCOT_TOP above it, the flukes RG_MASCOT_BOTTOM below. */
#define RG_MASCOT_W      32
#define RG_MASCOT_TOP    8
#define RG_MASCOT_BOTTOM 35

/* Badge size (the radio screens' header). */
#define RG_BADGE_W 14
#define RG_BADGE_H 9

/** Draw sprite @p id with its top left at (@p x, @p y). */
void rg_draw_sprite(Canvas* canvas, int32_t x, int32_t y, RgSpriteId id);

/**
 * Draw the dolphin of @p frame with its body's top left at (@p x, @p y).
 * Rows from @p clip_y down are not drawn (a desk in front of it, or the
 * screen's edge). @p fx draws the frame's effect too.
 */
void rg_draw_mascot(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const RgMascotFrame* frame,
    int32_t clip_y,
    bool fx);

/** Only the effect of @p frame (drawn last, over props and a desk). */
void rg_draw_mascot_fx(Canvas* canvas, int32_t x, int32_t y, const RgMascotFrame* frame);

/**
 * Draw @p prop standing on a desk whose top is at @p desk_y, its left edge
 * at @p x (about 15 pixels wide, up to 14 high). @p progress as in
 * RgMascotFrame: a computer shows a bar only for a known value.
 */
void rg_draw_prop(
    Canvas* canvas,
    int32_t x,
    int32_t desk_y,
    uint8_t prop,
    uint8_t phase,
    uint8_t progress);

/**
 * A lab decoration (RgDecoration of helpers/rg_journal.h: plant, poster,
 * lamp, mug, trophy) with its top left at (@p x, @p y), at most 11 x 10
 * pixels. 0 draws nothing.
 */
void rg_draw_decoration(Canvas* canvas, int32_t x, int32_t y, uint8_t decoration);

/** A radar scope of radius @p r at (@p cx, @p cy), its sweep at @p phase (0..15). */
void rg_draw_radar(Canvas* canvas, int32_t cx, int32_t cy, int32_t r, uint8_t phase);

/**
 * The radio screens' header badge (RG_BADGE_W x RG_BADGE_H at x, y): the
 * dolphin's head in Dolphin Lab (calm, alert on a detection, listening while
 * receiving, a lamp while recording), a small radar in Cyber Lab.
 * @p cyber picks the Cyber Lab badge.
 */
void rg_draw_badge(Canvas* canvas, int32_t x, int32_t y, bool cyber, const RgMascotFrame* frame);
