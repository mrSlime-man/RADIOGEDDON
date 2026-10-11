/**
 * Host canvas for the UI harness (make -C test ui): a 128x64 one-bit frame
 * buffer behind the stand-in gui/canvas.h and gui/view.h, with text drawn in
 * the firmware's own u8g2 fonts (decoded here from the font data that
 * test/ui/fetch_fonts.sh extracts), so text widths and pixels match the
 * device's.
 *
 * Every pixel a call tries to draw outside the screen is counted, and a text
 * that does not fit is logged with its position: tests assert none happen.
 *
 * The pictures are synthetic development previews of drawing code fed with
 * synthetic data; they are not screenshots of a Flipper Zero.
 */
#include "canvas_host.h"
#include <gui/elements.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include FONTS_INC

struct Canvas {
    uint8_t px[CANVAS_H][CANVAS_W]; // 1 = black
    Color color;
    const uint8_t* font;
    // Drawing outside the screen.
    uint32_t clipped;
    uint32_t text_overflows;
    char last_overflow[96];
    bool in_text;
    uint32_t text_clipped;
    bool bitmap_alpha;
};

static Canvas g_canvas;

Canvas* canvas_host(void) {
    return &g_canvas;
}

void canvas_host_reset(Canvas* c) {
    memset(c, 0, sizeof(*c));
    c->color = ColorBlack;
    c->font = u8g2_font_haxrcorp4089_tr;
}

uint32_t canvas_host_clipped(const Canvas* c) {
    return c->clipped;
}

uint32_t canvas_host_text_overflows(const Canvas* c) {
    return c->text_overflows;
}

const char* canvas_host_last_overflow(const Canvas* c) {
    return c->last_overflow;
}

bool canvas_host_pixel(const Canvas* c, int x, int y) {
    if(x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return false;
    return c->px[y][x] != 0;
}

uint32_t canvas_host_count(const Canvas* c, int x, int y, int w, int h) {
    uint32_t n = 0;
    for(int j = y; j < y + h; j++)
        for(int i = x; i < x + w; i++)
            n += canvas_host_pixel(c, i, j);
    return n;
}

static void canvas_host_set(Canvas* c, int32_t x, int32_t y) {
    if(x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) {
        c->clipped++;
        if(c->in_text) c->text_clipped++;
        return;
    }
    if(c->color == ColorXOR) {
        c->px[y][x] ^= 1;
    } else {
        c->px[y][x] = c->color == ColorBlack ? 1 : 0;
    }
}

bool canvas_host_write_pbm(const Canvas* c, const char* path) {
    FILE* f = fopen(path, "wb");
    if(!f) return false;
    fprintf(
        f,
        "P4\n# synthetic development preview, not a device screenshot\n%d %d\n",
        CANVAS_W,
        CANVAS_H);
    for(int y = 0; y < CANVAS_H; y++) {
        for(int x = 0; x < CANVAS_W; x += 8) {
            uint8_t b = 0;
            for(int k = 0; k < 8; k++)
                b = (uint8_t)((b << 1) | (c->px[y][x + k] ? 1 : 0));
            fputc(b, f);
        }
    }
    return fclose(f) == 0;
}

void canvas_host_dump(const Canvas* c) {
    for(int y = 0; y < CANVAS_H; y++) {
        for(int x = 0; x < CANVAS_W; x++)
            putchar(c->px[y][x] ? '#' : '.');
        putchar('\n');
    }
}

/* ---- canvas API ---------------------------------------------------------- */

void canvas_clear(Canvas* c) {
    memset(c->px, 0, sizeof(c->px));
}

void canvas_set_color(Canvas* c, Color color) {
    c->color = color;
}

void canvas_invert_color(Canvas* c) {
    c->color = c->color == ColorBlack ? ColorWhite : ColorBlack;
}

void canvas_set_font(Canvas* c, Font font) {
    switch(font) {
    case FontPrimary:
        c->font = u8g2_font_helvB08_tr;
        break;
    case FontKeyboard:
        c->font = u8g2_font_profont11_mr;
        break;
    default:
        // FontBigNumbers is not drawn by the views under test.
        c->font = u8g2_font_haxrcorp4089_tr;
        break;
    }
}

void canvas_draw_dot(Canvas* c, int32_t x, int32_t y) {
    canvas_host_set(c, x, y);
}

void canvas_draw_line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int32_t dy = y2 > y1 ? y1 - y2 : y2 - y1;
    int32_t sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int32_t err = dx + dy;
    for(;;) {
        canvas_host_set(c, x1, y1);
        if(x1 == x2 && y1 == y2) break;
        int32_t e2 = 2 * err;
        if(e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if(e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void canvas_draw_box(Canvas* c, int32_t x, int32_t y, size_t width, size_t height) {
    for(size_t j = 0; j < height; j++)
        for(size_t i = 0; i < width; i++)
            canvas_host_set(c, x + (int32_t)i, y + (int32_t)j);
}

void canvas_draw_frame(Canvas* c, int32_t x, int32_t y, size_t width, size_t height) {
    if(!width || !height) return;
    int32_t r = x + (int32_t)width - 1, b = y + (int32_t)height - 1;
    canvas_draw_line(c, x, y, r, y);
    canvas_draw_line(c, x, b, r, b);
    canvas_draw_line(c, x, y, x, b);
    canvas_draw_line(c, r, y, r, b);
}

void canvas_draw_xbm(
    Canvas* c,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    const uint8_t* bitmap) {
    // XBM: rows of (width + 7) / 8 bytes, least significant bit leftmost;
    // only set bits are drawn (u8g2's transparent bitmap mode is off on the
    // device, where clear bits draw the background: same result on a
    // cleared screen).
    size_t stride = (width + 7u) / 8u;
    Color color = c->color;
    for(size_t j = 0; j < height; j++)
        for(size_t i = 0; i < width; i++) {
            bool set = bitmap[j * stride + i / 8u] & (1u << (i % 8u));
            if(set) {
                canvas_host_set(c, x + (int32_t)i, y + (int32_t)j);
            } else if(!c->bitmap_alpha) {
                // Solid mode, as the firmware draws by default: clear bits
                // in the background colour.
                c->color = color == ColorBlack ? ColorWhite : ColorBlack;
                if(color != ColorXOR) canvas_host_set(c, x + (int32_t)i, y + (int32_t)j);
                c->color = color;
            }
        }
}

void canvas_set_bitmap_mode(Canvas* c, bool alpha) {
    c->bitmap_alpha = alpha;
}

/* u8g2's circle: the midpoint algorithm, eight points per step (u8g2_circle.c). */
static void
    canvas_host_circle_points(Canvas* c, int32_t x0, int32_t y0, int32_t x, int32_t y, bool fill) {
    if(fill) {
        for(int32_t k = y0 - y; k <= y0 + y; k++) {
            canvas_host_set(c, x0 + x, k);
            canvas_host_set(c, x0 - x, k);
        }
        for(int32_t k = y0 - x; k <= y0 + x; k++) {
            canvas_host_set(c, x0 + y, k);
            canvas_host_set(c, x0 - y, k);
        }
        return;
    }
    canvas_host_set(c, x0 + x, y0 - y);
    canvas_host_set(c, x0 + y, y0 - x);
    canvas_host_set(c, x0 - x, y0 - y);
    canvas_host_set(c, x0 - y, y0 - x);
    canvas_host_set(c, x0 + x, y0 + y);
    canvas_host_set(c, x0 + y, y0 + x);
    canvas_host_set(c, x0 - x, y0 + y);
    canvas_host_set(c, x0 - y, y0 + x);
}

static void canvas_host_circle(Canvas* c, int32_t x0, int32_t y0, int32_t rad, bool fill) {
    int32_t f = 1 - rad, ddx = 1, ddy = -2 * rad, x = 0, y = rad;
    canvas_host_circle_points(c, x0, y0, x, y, fill);
    while(x < y) {
        if(f >= 0) {
            y--;
            ddy += 2;
            f += ddy;
        }
        x++;
        ddx += 2;
        f += ddx;
        canvas_host_circle_points(c, x0, y0, x, y, fill);
    }
}

void canvas_draw_circle(Canvas* c, int32_t x, int32_t y, size_t radius) {
    canvas_host_circle(c, x, y, (int32_t)radius, false);
}

void canvas_draw_disc(Canvas* c, int32_t x, int32_t y, size_t radius) {
    canvas_host_circle(c, x, y, (int32_t)radius, true);
}

/* Rounded rectangles: straight sides and quarter circles in the corners. */
static void canvas_host_rounded(
    Canvas* c,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    size_t radius,
    bool fill) {
    int32_t w = (int32_t)width, h = (int32_t)height, r = (int32_t)radius;
    if(w <= 0 || h <= 0) return;
    if(2 * r >= w) r = (w - 1) / 2;
    if(2 * r >= h) r = (h - 1) / 2;
    if(r < 0) r = 0;
    for(int32_t j = 0; j < h; j++)
        for(int32_t i = 0; i < w; i++) {
            // Distance into the nearest corner's square.
            int32_t cx = i < r ? r - i : (i >= w - r ? i - (w - 1 - r) : 0);
            int32_t cy = j < r ? r - j : (j >= h - r ? j - (h - 1 - r) : 0);
            int32_t d2 = cx * cx + cy * cy;
            bool inside = d2 <= r * r + r / 2;
            if(!inside) continue;
            bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1;
            if(!edge && (cx || cy)) {
                // On a corner's arc: inside, but a neighbour outside.
                int32_t ox = cx ? cx + 1 : 0, oy = cy ? cy + 1 : 0;
                edge = ox * ox + cy * cy > r * r + r / 2 || cx * cx + oy * oy > r * r + r / 2;
            }
            if(fill || edge) canvas_host_set(c, x + i, y + j);
        }
}

void canvas_draw_rframe(
    Canvas* c,
    int32_t x,
    int32_t y,
    size_t width,
    size_t height,
    size_t radius) {
    canvas_host_rounded(c, x, y, width, height, radius, false);
}

void canvas_draw_rbox(Canvas* c, int32_t x, int32_t y, size_t width, size_t height, size_t radius) {
    canvas_host_rounded(c, x, y, width, height, radius, true);
}

/* ---- u8g2 font decoding (font format of u8g2's u8g2_font.c) -------------- */

typedef struct {
    const uint8_t* ptr;
    uint8_t bit_pos;
} U8Bits;

static uint32_t u8_bits(U8Bits* d, uint8_t cnt) {
    if(cnt == 0) return 0;
    uint32_t val = (uint32_t)(d->ptr[0] >> d->bit_pos);
    uint8_t end = (uint8_t)(d->bit_pos + cnt);
    if(end >= 8) {
        uint8_t s = (uint8_t)(8 - d->bit_pos);
        d->ptr++;
        val |= (uint32_t)d->ptr[0] << s;
        end = (uint8_t)(end - 8);
    }
    d->bit_pos = end;
    return val & ((1u << cnt) - 1u);
}

static int32_t u8_sbits(U8Bits* d, uint8_t cnt) {
    int32_t v = (int32_t)u8_bits(d, cnt);
    return v - (cnt ? (1 << (cnt - 1)) : 0);
}

static const uint8_t* u8_glyph(const uint8_t* font, uint8_t enc) {
    const uint8_t* p = font + 23;
    if(enc >= 'a') {
        p += (font[19] << 8) | font[20];
    } else if(enc >= 'A') {
        p += (font[17] << 8) | font[18];
    }
    for(;;) {
        if(p[1] == 0) return NULL;
        if(p[0] == enc) return p + 2;
        p += p[1];
    }
}

typedef struct {
    uint32_t w, h;
    int32_t x, y, dx;
} U8Head;

static void u8_head(const uint8_t* font, U8Bits* d, U8Head* g) {
    g->w = u8_bits(d, font[4]);
    g->h = u8_bits(d, font[5]);
    g->x = u8_sbits(d, font[6]);
    g->y = u8_sbits(d, font[7]);
    g->dx = u8_sbits(d, font[8]);
}

/* Draw one glyph with its baseline at y; returns its advance. */
static int32_t u8_draw_glyph(Canvas* c, int32_t x, int32_t y, uint8_t enc) {
    const uint8_t* font = c->font;
    const uint8_t* data = u8_glyph(font, enc);
    if(!data) return 0;
    U8Bits d = {data, 0};
    U8Head g;
    u8_head(font, &d, &g);
    if(g.w == 0) return g.dx;
    int32_t tx = x + g.x;
    int32_t ty = y - (int32_t)g.h - g.y;
    uint32_t gx = 0, gy = 0;
    for(;;) {
        uint32_t a = u8_bits(&d, font[2]);
        uint32_t b = u8_bits(&d, font[3]);
        do {
            for(int fg = 0; fg < 2; fg++) {
                uint32_t len = fg ? b : a;
                while(len > 0) {
                    uint32_t rem = g.w - gx;
                    uint32_t cnt = len < rem ? len : rem;
                    if(fg)
                        for(uint32_t k = 0; k < cnt; k++)
                            canvas_host_set(c, tx + (int32_t)(gx + k), ty + (int32_t)gy);
                    len -= cnt;
                    gx += cnt;
                    if(gx >= g.w) {
                        gx = 0;
                        gy++;
                    }
                }
            }
        } while(u8_bits(&d, 1) != 0);
        if(gy >= g.h) break;
    }
    return g.dx;
}

uint16_t canvas_string_width(Canvas* c, const char* str) {
    int32_t w = 0, dx = 0;
    U8Head last = {0, 0, 0, 0, 0};
    for(const char* s = str; *s; s++) {
        const uint8_t* data = u8_glyph(c->font, (uint8_t)*s);
        if(!data) continue;
        U8Bits d = {data, 0};
        u8_head(c->font, &d, &last);
        dx = last.dx;
        w += dx;
    }
    // As u8g2_GetStrWidth: the last glyph counts its ink, not its advance.
    if(last.w != 0) {
        w -= dx;
        w += (int32_t)last.w + last.x;
    }
    return (uint16_t)(w < 0 ? 0 : w);
}

static void canvas_host_text(Canvas* c, int32_t x, int32_t y, const char* str) {
    c->in_text = true;
    c->text_clipped = 0;
    for(const char* s = str; *s; s++)
        x += u8_draw_glyph(c, x, y, (uint8_t)*s);
    c->in_text = false;
    if(c->text_clipped) {
        c->text_overflows++;
        snprintf(c->last_overflow, sizeof(c->last_overflow), "\"%s\" at y=%d", str, (int)y);
    }
}

void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* str) {
    if(str) canvas_host_text(c, x, y, str);
}

void canvas_draw_str_aligned(
    Canvas* c,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str) {
    if(!str) return;
    if(horizontal == AlignRight) {
        x -= canvas_string_width(c, str);
    } else if(horizontal == AlignCenter) {
        x -= canvas_string_width(c, str) / 2;
    }
    int32_t ascent = (int8_t)c->font[13];
    if(vertical == AlignTop) {
        y += ascent;
    } else if(vertical == AlignCenter) {
        y += ascent / 2;
    }
    canvas_host_text(c, x, y, str);
}

/* ---- views ----------------------------------------------------------------- */

struct View {
    ViewDrawCallback draw;
    ViewInputCallback input;
    ViewCallback enter;
    ViewCallback exit;
    void* context;
    void* model;
    uint32_t updates;
};

View* view_alloc(void) {
    return calloc(1, sizeof(View));
}

void view_free(View* view) {
    free(view->model);
    free(view);
}

void view_set_context(View* view, void* context) {
    view->context = context;
}

void view_allocate_model(View* view, ViewModelType type, size_t size) {
    (void)type;
    view->model = calloc(1, size);
}

void view_set_draw_callback(View* view, ViewDrawCallback callback) {
    view->draw = callback;
}

void view_set_input_callback(View* view, ViewInputCallback callback) {
    view->input = callback;
}

void view_set_enter_callback(View* view, ViewCallback callback) {
    view->enter = callback;
}

void view_set_exit_callback(View* view, ViewCallback callback) {
    view->exit = callback;
}

void view_host_enter(View* view) {
    if(view->enter) view->enter(view->context);
}

void view_host_exit(View* view) {
    if(view->exit) view->exit(view->context);
}

uint32_t view_host_updates(View* view) {
    uint32_t n = view->updates;
    view->updates = 0;
    return n;
}

void* view_get_model(View* view) {
    return view->model;
}

void view_commit_model(View* view, bool update) {
    if(update) view->updates++;
}

void view_host_draw(View* view, Canvas* canvas) {
    // As the GUI before each draw callback.
    canvas->color = ColorBlack;
    canvas->bitmap_alpha = false;
    canvas_set_font(canvas, FontSecondary);
    view->draw(canvas, view->model);
}

bool view_host_input(View* view, InputKey key, InputType type) {
    InputEvent e = {.sequence = 0, .key = key, .type = type};
    return view->input ? view->input(&e, view->context) : false;
}

/* As the firmware's elements_scrollbar_pos: a dotted track with a block
 * whose position tells where the list is. */
void elements_scrollbar_pos(
    Canvas* c,
    int32_t x,
    int32_t y,
    size_t height,
    size_t pos,
    size_t total) {
    if(total < 2 || height == 0) return;
    size_t block = height / total;
    if(block < 1) block = 1;
    if(pos >= total) pos = total - 1;
    int32_t top = y + (int32_t)((height - block) * pos / (total - 1));
    for(int32_t yy = y; yy < y + (int32_t)height; yy += 2)
        canvas_host_set(c, x - 2, yy);
    canvas_draw_box(c, x - 3, top, 3, block);
}

/* As the firmware's elements_button_center: a box at the bottom centre with
 * an OK mark (here a ring) and the label in the inverted colour. */
void elements_button_center(Canvas* c, const char* str) {
    int32_t w = (int32_t)canvas_string_width(c, str) + 2 + 10;
    int32_t x = (128 - w) / 2, y = 64;
    canvas_draw_box(c, x, y - 12, (size_t)w, 12);
    for(int32_t k = 1; k <= 3; k++) {
        canvas_draw_line(c, x - k, y - 1, x - k, y - 12 + k - 1);
        canvas_draw_line(c, x + w - 1 + k, y - 1, x + w - 1 + k, y - 12 + k - 1);
    }
    canvas_invert_color(c);
    canvas_draw_circle(c, x + 4, y - 6, 3);
    canvas_draw_str(c, x + 11, y - 3, str);
    canvas_invert_color(c);
}
