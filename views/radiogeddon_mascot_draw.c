#include "radiogeddon_mascot_draw.h"
#include "../radiogeddon_edition.h" /* RG_VIEW_TEXT */

#if RG_FEATURE_DOLPHIN_LAB

/* Anchors on the 32x32 body (art/dolphin/dolphin.txt). */
#define FLUKE_X   0
#define FLUKE_Y   24
#define FIN_X     13
#define FIN_Y     17
#define EYE_X     16
#define EYE_Y     7
#define PHONES_X  8
#define PHONES_Y  2
#define GOGGLES_X 16
#define GOGGLES_Y 1
/* The antenna rises from the headphone band. */
#define ANTENNA_X 12
#define ANTENNA_Y 2

/* Sixteen directions, clockwise from straight up, scaled by 64. */
static const int8_t rg_dir_x[16] =
    {0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24};
static const int8_t rg_dir_y[16] =
    {-64, -59, -45, -24, 0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59};

static const uint8_t rg_eye_sprite[RgMascotEyesCount] = {
    RgSpriteEyeOpen,
    RgSpriteEyeBlink,
    RgSpriteEyeHappy,
    RgSpriteEyeWide,
    RgSpriteEyeWorried,
    RgSpriteEyeSleepy,
    RgSpriteEyeShut,
    RgSpriteEyeFocus,
};

static const uint8_t rg_fin_sprite[RgMascotFinCount] = {
    RgSpriteFinRest,
    RgSpriteFinPoint,
    RgSpriteFinCheer,
};

/* The first @p rows rows of sprite @p id (rows are whole bytes, so a prefix
 * of the data is a smaller bitmap). */
static RG_VIEW_TEXT void
    rg_draw_sprite_rows(Canvas* canvas, int32_t x, int32_t y, RgSpriteId id, int32_t rows) {
    if((unsigned)id >= RgSpriteCount) return;
    const RgSprite* s = &rg_sprites[id];
    if(rows > s->h) rows = s->h;
    if(rows <= 0) return;
    canvas_set_bitmap_mode(canvas, true);
    canvas_draw_xbm(canvas, x, y, s->w, (size_t)rows, rg_sprite_bits + s->offset);
    canvas_set_bitmap_mode(canvas, false);
}

RG_VIEW_TEXT void rg_draw_sprite(Canvas* canvas, int32_t x, int32_t y, RgSpriteId id) {
    if((unsigned)id >= RgSpriteCount) return;
    rg_draw_sprite_rows(canvas, x, y, id, rg_sprites[id].h);
}

/* A sprite clipped to the rows above @p clip_y. */
static RG_VIEW_TEXT void
    rg_draw_clipped(Canvas* canvas, int32_t x, int32_t y, RgSpriteId id, int32_t clip_y) {
    rg_draw_sprite_rows(canvas, x, y, id, clip_y - y);
}

static RG_VIEW_TEXT void rg_draw_bubble(Canvas* canvas, int32_t x, int32_t y, const char* mark) {
    // A white bubble with a tail towards the dolphin, the mark inside.
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, 9, 11);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, x, y, 9, 11, 2);
    canvas_draw_line(canvas, x, y + 9, x - 2, y + 11);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, x + 5, y + 10, AlignCenter, AlignBottom, mark);
}

static RG_VIEW_TEXT void rg_draw_fx(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const RgMascotFrame* f,
    int32_t ax,
    int32_t ay) {
    if(!f->fx_on && f->fx != RgMascotFxRec && f->fx != RgMascotFxChip) return;
    switch(f->fx) {
    case RgMascotFxExclaim:
        rg_draw_bubble(canvas, x + 23, y - 7, "!");
        break;
    case RgMascotFxQuestion:
        rg_draw_bubble(canvas, x + 23 + (f->fx_phase & 1), y - 7, "?");
        break;
    case RgMascotFxZzz:
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, x + 21, y + 6 - (f->fx_phase & 1), "z");
        if(f->fx_phase >= 1) canvas_draw_str(canvas, x + 25, y + 2, "z");
        if(f->fx_phase >= 2) canvas_draw_str(canvas, x + 29, y - 1 + (f->fx_phase & 1), "Z");
        break;
    case RgMascotFxSparkle: {
        static const int8_t px[4][2] = {{24, -6}, {1, 2}, {27, 6}, {0, -5}};
        rg_draw_sprite(
            canvas, x + px[f->fx_phase & 3][0], y + px[f->fx_phase & 3][1], RgSpriteSparkle);
        rg_draw_sprite(
            canvas,
            x + px[(f->fx_phase + 2) & 3][0],
            y + px[(f->fx_phase + 2) & 3][1],
            RgSpriteSparkle);
        break;
    }
    case RgMascotFxWaves: {
        // Radio waves off the antenna tip: one, two, then three arcs.
        uint8_t arcs = (uint8_t)(f->fx_phase % 3u + 1u);
        rg_draw_sprite(canvas, ax + 2, ay - 1, RgSpriteWaveS);
        if(arcs > 1) rg_draw_sprite(canvas, ax + 4, ay - 2, RgSpriteWaveM);
        if(arcs > 2) rg_draw_sprite(canvas, ax + 6, ay - 3, RgSpriteWaveL);
        break;
    }
    case RgMascotFxRec:
        // The label always shows while recording; the lamp blinks.
        canvas_set_color(canvas, ColorBlack);
        // The lamp first, then the label: [o REC].
        canvas_draw_rbox(canvas, x + 20, y - 7, 25, 9, 2);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        if(f->fx_on) canvas_draw_disc(canvas, x + 24, y - 3, 1);
        canvas_draw_str(canvas, x + 27, y + 0, "REC");
        canvas_set_color(canvas, ColorBlack);
        break;
    case RgMascotFxChip:
        if(f->fx_on) {
            rg_draw_sprite(canvas, x + 23, y - 6, RgSpriteChip);
        } else {
            canvas_draw_frame(canvas, x + 23, y - 5, 9, 5);
        }
        break;
    default:
        break;
    }
}

RG_VIEW_TEXT void rg_draw_mascot(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const RgMascotFrame* f,
    int32_t clip_y,
    bool fx) {
    y += f->body_dy;
    canvas_set_color(canvas, ColorBlack);
    rg_draw_clipped(canvas, x, y, RgSpriteBody, clip_y);
    rg_draw_clipped(
        canvas,
        x + FLUKE_X,
        y + FLUKE_Y,
        f->fluke_flick ? RgSpriteFlukeFlick : RgSpriteFlukeRest,
        clip_y);
    uint8_t fin = f->fin < RgMascotFinCount ? f->fin : RgMascotFinRest;
    rg_draw_clipped(canvas, x + FIN_X, y + FIN_Y, rg_fin_sprite[fin], clip_y);
    uint8_t eyes = f->eyes < RgMascotEyesCount ? f->eyes : RgMascotEyesOpen;
    rg_draw_clipped(
        canvas, x + EYE_X + f->eye_dx, y + EYE_Y + f->eye_dy, rg_eye_sprite[eyes], clip_y);
    if(f->goggles) rg_draw_clipped(canvas, x + GOGGLES_X, y + GOGGLES_Y, RgSpriteGoggles, clip_y);
    int32_t lift = f->phones_lift > 4 ? 4 : f->phones_lift;
    // Without headphones the antenna stands on the head itself.
    if(f->phones) {
        rg_draw_clipped(canvas, x + PHONES_X, y + PHONES_Y - lift, RgSpritePhones, clip_y);
    } else {
        lift = -1;
    }
    // Accessories at the throat and chest.
    if(f->accessory == RgLookAccessoryBowtie)
        rg_draw_clipped(canvas, x + 19, y + 16, RgSpriteBowtie, clip_y);
    if(f->accessory == RgLookAccessoryScarf)
        rg_draw_clipped(canvas, x + 7, y + 15, RgSpriteScarf, clip_y);
    if(f->accessory == RgLookAccessoryBadge)
        rg_draw_clipped(canvas, x + 15, y + 19, RgSpriteIdBadge, clip_y);

    // Antenna: a mast leaning back from the band, a knob on top (or a long
    // whip, or a small dish).
    int32_t len = f->antenna < 3 ? 3 : (f->antenna > 7 ? 7 : f->antenna);
    int32_t ax0 = x + ANTENNA_X, ay0 = y + ANTENNA_Y - lift;
    int32_t ax = ax0 - len / 2, ay = ay0 - len;
    switch(f->antenna_kind) {
    case RgLookAntennaWhip:
        ax = ax0 - (len + 3) / 2;
        ay = ay0 - (len + 3);
        canvas_draw_line(canvas, ax0, ay0, ax, ay);
        canvas_draw_dot(canvas, ax, ay - 1);
        break;
    case RgLookAntennaDish:
        canvas_draw_line(canvas, ax0, ay0, ax, ay);
        rg_draw_sprite(canvas, ax - 2, ay - 4, RgSpriteDish);
        ay -= 2;
        break;
    case RgLookAntennaNone:
        ax = ax0;
        ay = ay0;
        break;
    default:
        canvas_draw_line(canvas, ax0, ay0, ax, ay);
        canvas_draw_box(canvas, ax - 1, ay - 2, 2, 2);
        break;
    }

    // A tool at the flipper's tip, when it holds nothing else.
    if(f->prop == RgMascotPropNone && f->fin != RgMascotFinCheer) {
        static const uint8_t tools[RgLookToolCount] = {
            0, RgSpriteWrench, RgSpriteProbe, RgSpriteNotebook};
        if(f->tool > RgLookToolNone && f->tool < RgLookToolCount)
            rg_draw_clipped(canvas, x + 23, y + 17, (RgSpriteId)tools[f->tool], clip_y);
    }

    if(f->prop == RgMascotPropRadio) {
        // Held up in the flipper.
        rg_draw_clipped(canvas, x + 24, y + 13, RgSpriteRadio, clip_y);
        if(f->prop_phase & 1) canvas_draw_dot(canvas, x + 27, y + 15);
    }
    if(fx) rg_draw_fx(canvas, x, y, f, ax, ay - 2);
}

RG_VIEW_TEXT void rg_draw_mascot_fx(Canvas* canvas, int32_t x, int32_t y, const RgMascotFrame* f) {
    y += f->body_dy;
    int32_t lift = f->phones ? (f->phones_lift > 4 ? 4 : f->phones_lift) : -1;
    int32_t len = f->antenna < 3 ? 3 : (f->antenna > 7 ? 7 : f->antenna);
    canvas_set_color(canvas, ColorBlack);
    rg_draw_fx(canvas, x, y, f, x + ANTENNA_X - len / 2, y + ANTENNA_Y - lift - len - 2);
}

RG_VIEW_TEXT void rg_draw_radar(Canvas* canvas, int32_t cx, int32_t cy, int32_t r, uint8_t phase) {
    canvas_draw_circle(canvas, cx, cy, (size_t)r);
    canvas_draw_dot(canvas, cx, cy);
    uint8_t k = phase & 15u;
    canvas_draw_line(
        canvas, cx, cy, cx + rg_dir_x[k] * (r - 1) / 64, cy + rg_dir_y[k] * (r - 1) / 64);
    // A fading trail one step behind.
    uint8_t k1 = (uint8_t)((k + 15u) & 15u);
    canvas_draw_dot(canvas, cx + rg_dir_x[k1] * (r - 2) / 64, cy + rg_dir_y[k1] * (r - 2) / 64);
}

RG_VIEW_TEXT void rg_draw_prop(
    Canvas* canvas,
    int32_t x,
    int32_t desk_y,
    uint8_t prop,
    uint8_t phase,
    uint8_t progress) {
    canvas_set_color(canvas, ColorBlack);
    switch(prop) {
    case RgMascotPropRadar:
        // A dish on a stand.
        rg_draw_radar(canvas, x + 7, desk_y - 8, 6, phase);
        canvas_draw_line(canvas, x + 7, desk_y - 1, x + 7, desk_y - 2);
        canvas_draw_line(canvas, x + 4, desk_y - 1, x + 10, desk_y - 1);
        break;
    case RgMascotPropScope: {
        // A small oscilloscope; its trace is a drawing, not a measurement.
        int32_t top = desk_y - 11;
        canvas_draw_rframe(canvas, x, top, 15, 11, 1);
        for(int32_t i = 0; i < 11; i++) {
            int32_t k = (i * 3 + phase * 2) & 15;
            canvas_draw_dot(canvas, x + 2 + i, top + 5 + rg_dir_y[k] * 3 / 64);
        }
        break;
    }
    case RgMascotPropRecorder: {
        int32_t top = desk_y - 9;
        canvas_draw_rframe(canvas, x, top, 15, 9, 1);
        static const int8_t spoke[4][2] = {{0, -2}, {2, 0}, {0, 2}, {-2, 0}};
        for(int32_t r = 0; r < 2; r++) {
            int32_t cx = x + 4 + r * 7, cy = top + 4;
            canvas_draw_circle(canvas, cx, cy, 2);
            uint8_t s = (uint8_t)((phase + r) & 3u);
            canvas_draw_line(canvas, cx, cy, cx + spoke[s][0], cy + spoke[s][1]);
        }
        break;
    }
    case RgMascotPropComputer: {
        int32_t top = desk_y - 13;
        canvas_draw_rframe(canvas, x, top, 15, 11, 1);
        canvas_draw_line(canvas, x + 7, top + 11, x + 7, desk_y - 1);
        canvas_draw_line(canvas, x + 4, desk_y - 1, x + 10, desk_y - 1);
        if(progress <= 100) {
            // Real progress only: a bar of what is done.
            canvas_draw_frame(canvas, x + 2, top + 6, 11, 3);
            int32_t w = (int32_t)progress * 9 / 100;
            if(w > 0) canvas_draw_line(canvas, x + 3, top + 7, x + 2 + w, top + 7);
            canvas_draw_line(canvas, x + 2, top + 3, x + 7, top + 3);
        } else {
            // Unknown progress: lines of text scrolling past.
            for(int32_t l = 0; l < 3; l++) {
                int32_t len = 3 + ((l + phase) * 5) % 8;
                canvas_draw_line(canvas, x + 2, top + 2 + l * 3, x + 2 + len, top + 2 + l * 3);
            }
        }
        break;
    }
    default:
        break;
    }
}

RG_VIEW_TEXT void rg_draw_decoration(Canvas* canvas, int32_t x, int32_t y, uint8_t decoration) {
    static const uint8_t sprites[] = {
        0,
        RgSpriteDecoPlant,
        RgSpriteDecoPoster,
        RgSpriteDecoLamp,
        RgSpriteDecoMug,
        RgSpriteDecoTrophy,
    };
    if(decoration == 0 || decoration >= sizeof(sprites)) return;
    canvas_set_color(canvas, ColorBlack);
    rg_draw_sprite(canvas, x, y, (RgSpriteId)sprites[decoration]);
}

RG_VIEW_TEXT void
    rg_draw_badge(Canvas* canvas, int32_t x, int32_t y, bool cyber, const RgMascotFrame* f) {
    canvas_set_color(canvas, ColorBlack);
    if(cyber) {
        rg_draw_radar(canvas, x + 4, y + 4, 4, f->prop_phase);
        if(f->state == RgMascotStateRecording && f->fx_on)
            canvas_draw_disc(canvas, x + 12, y + 4, 1);
        if(f->state == RgMascotStateDetected || f->fx == RgMascotFxExclaim)
            canvas_draw_line(canvas, x + 12, y, x + 12, y + 5);
        return;
    }
    RgSpriteId head = RgSpriteMiniCalm;
    if(f->state == RgMascotStateReceiving || f->state == RgMascotStateRecording)
        head = RgSpriteMiniListen;
    if(f->eyes == RgMascotEyesWide) head = RgSpriteMiniAlert;
    rg_draw_sprite(canvas, x, y + 1, head);
    if(f->state == RgMascotStateRecording) {
        // The lamp follows the recorder; it blinks while a capture runs.
        if(f->fx_on) {
            canvas_draw_disc(canvas, x + 12, y + 2, 1);
        } else {
            canvas_draw_circle(canvas, x + 12, y + 2, 1);
        }
    } else if(f->fx == RgMascotFxExclaim && f->fx_on) {
        canvas_draw_line(canvas, x + 12, y, x + 12, y + 4);
        canvas_draw_dot(canvas, x + 12, y + 6);
    } else if(f->state == RgMascotStateReceiving) {
        uint8_t arcs = (uint8_t)(f->fx_phase % 3u);
        canvas_draw_dot(canvas, x + 11, y + 1);
        if(arcs >= 1) canvas_draw_line(canvas, x + 12, y, x + 12, y + 2);
        if(arcs >= 2) canvas_draw_line(canvas, x + 13, y, x + 13, y + 3);
    } else if(f->state == RgMascotStateScanning) {
        // The antenna ticks round with the sweep.
        canvas_draw_line(canvas, x + 12, y + 1, x + 12 + ((f->prop_phase >> 2) & 1), y);
    }
}

#endif
