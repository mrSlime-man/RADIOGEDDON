#include "radiogeddon_studio_view.h"
#include "../helpers/rg_div.h"

#if RG_FEATURE_PULSE_STUDIO

#include "../helpers/rg_studio.h"
#include <furi.h>
#include <gui/elements.h>

#define WAVE_HI    15
#define WAVE_LO    31
#define WAVE_COLS  128
#define BITS_Y     42
#define TEXT_CHARS 25
#define TEXT_LINES 5

typedef enum {
    StudioPageWave,
    StudioPageFrame,
    StudioPageBits,
    StudioPageHex,
    StudioPageDiff,
    StudioPageTiming,
    StudioPageCount,
} StudioPage;

static const char* const studio_page_names[StudioPageCount] = {
    "Wave",
    "Frame",
    "Bits",
    "Hex",
    "Diff",
    "Timing",
};

typedef struct {
    uint64_t start_us;
    uint32_t duration_us;
    uint16_t samples;
    uint16_t bit_count;
    uint16_t repeat_bits;
    uint8_t fit;
    uint8_t group;
    int8_t shift;
    bool truncated;
} StudioFrameInfo;

struct RadioGeddonStudioView {
    View* view;
    RadioGeddonStudioCallback callback;
    void* context;
};

typedef struct {
    char name[24];
    RgDecodeParams params;
    uint8_t encoding;
    uint8_t frames;
    uint8_t frame; // shown
    bool loaded; // frame's samples are in f
    StudioFrameInfo info[RG_ANALYZER_MAX_FRAMES];
    uint16_t group_exact[RG_ANALYZER_MAX_GROUPS];
    char ref[RG_ANALYZER_MAX_BITS + 1]; // the most common pattern
    uint16_t ref_n;
    uint8_t page;
    uint8_t zoom;
    uint64_t view_us; // waveform window start
    uint16_t cursor; // sample under the cursor
    int32_t marker; // sample of the measuring marker, -1 none
    uint16_t scroll; // text pages' first line
    RgStudioFrame f;
} StudioModel;

/* ---- drawing --------------------------------------------------------------- */

static void studio_header(Canvas* canvas, StudioModel* m) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 10);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    char left[16];
    snprintf(left, sizeof(left), "F%u/%u", (unsigned)(m->frame + 1), (unsigned)m->frames);
    canvas_draw_str(canvas, 2, 8, left);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, studio_page_names[m->page]);
    char right[16];
    snprintf(
        right,
        sizeof(right),
        "%s %u",
        m->encoding == RgEncodingManchester ? "Manch" :
                                              rg_analyzer_encoding_name((RgEncoding)m->encoding),
        (unsigned)m->f.nbits);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, right);
    canvas_set_color(canvas, ColorBlack);
}

static int32_t studio_x(StudioModel* m, uint64_t t) {
    uint32_t per = rg_studio_zoom_us(m->zoom);
    if(t < m->view_us) return -1;
    uint64_t x = rg_div_u64(t - m->view_us, per);
    return x > 1000 ? 1000 : (int32_t)x;
}

static void studio_draw_wave(Canvas* canvas, StudioModel* m) {
    const RgStudioFrame* f = &m->f;
    uint64_t t = 0;
    bool prev_hi = false;
    for(uint16_t i = 0; i < f->n; i++) {
        uint32_t d = f->s[i] < 0 ? (uint32_t)(-(int64_t)f->s[i]) : (uint32_t)f->s[i];
        bool hi = f->s[i] > 0;
        int32_t x0 = studio_x(m, t), x1 = studio_x(m, t + d);
        t += d;
        if(x1 < 0) {
            prev_hi = hi;
            continue;
        }
        if(x0 >= WAVE_COLS) break;
        if(x0 < 0) x0 = 0;
        if(x1 > WAVE_COLS - 1) x1 = WAVE_COLS - 1;
        int32_t y = hi ? WAVE_HI : WAVE_LO;
        if(i > 0 && hi != prev_hi && x0 > 0) canvas_draw_line(canvas, x0, WAVE_HI, x0, WAVE_LO);
        canvas_draw_line(canvas, x0, y, x1, y);
        if(i == m->cursor) {
            // The cursor: a bracket under the pulse or gap.
            canvas_draw_line(canvas, x0, WAVE_LO + 3, x1, WAVE_LO + 3);
            canvas_draw_line(canvas, x0, WAVE_LO + 2, x0, WAVE_LO + 4);
            canvas_draw_line(canvas, x1, WAVE_LO + 2, x1, WAVE_LO + 4);
        }
        if(rg_studio_glitch(f, i)) canvas_draw_dot(canvas, x0, WAVE_LO + 1);
        if(f->preamble && i < f->preamble) {
            // Preamble: a dashed line above it.
            for(int32_t x = x0; x <= x1; x += 2)
                canvas_draw_dot(canvas, x, 12);
        }
        if((int32_t)i == m->marker) {
            for(int32_t y2 = 11; y2 <= WAVE_LO + 4; y2 += 2)
                canvas_draw_dot(canvas, x0, y2);
            canvas_draw_str(canvas, x0 + 1 > 122 ? 122 : x0 + 1, 21, "A");
        }
        prev_hi = hi;
    }
    // Frame start and end (where the gap that ended it begins).
    int32_t xs = studio_x(m, 0), xe = studio_x(m, f->span_us);
    if(xs >= 0 && xs < WAVE_COLS) canvas_draw_line(canvas, xs, 11, xs, 13);
    if(xe >= 0 && xe < WAVE_COLS) {
        for(int32_t y = 11; y <= WAVE_LO; y += 3)
            canvas_draw_dot(canvas, xe, y);
    }
    // The bits under their symbols, where there is room.
    canvas_set_font(canvas, FontSecondary);
    bool unsure = false;
    uint16_t sel = rg_studio_bit_of(f, m->cursor, &unsure);
    int32_t last = -10;
    for(uint16_t b = 0; b < f->nbits; b++) {
        uint16_t first, end;
        if(!rg_studio_bit_span(f, b, &first, &end)) continue;
        int32_t x = studio_x(m, rg_studio_time_at(f, first));
        if(x < 0) continue;
        if(x > WAVE_COLS - 5) break;
        if(x < last + 6 && b != sel) continue;
        char c[2] = {(f->map[b] & RG_BITMAP_UNSURE) ? '?' : f->bits[b], '\0'};
        if(b == sel) {
            canvas_draw_box(canvas, x > 0 ? x - 1 : 0, BITS_Y - 8, 7, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, x, BITS_Y, c);
        canvas_set_color(canvas, ColorBlack);
        last = x;
    }
    // Footer: the cursor's pulse, its bit and the marker's distance.
    char line[64];
    if(m->cursor < f->n) {
        int32_t v = f->s[m->cursor];
        uint32_t d = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
        if(f->te_us) {
            snprintf(
                line,
                sizeof(line),
                "#%u %s %luus %lu.%luTe",
                (unsigned)m->cursor,
                v > 0 ? "pulse" : "gap",
                (unsigned long)d,
                (unsigned long)(d / f->te_us),
                (unsigned long)(d * 10u / f->te_us % 10u));
        } else {
            snprintf(
                line,
                sizeof(line),
                "#%u %s %luus",
                (unsigned)m->cursor,
                v > 0 ? "pulse" : "gap",
                (unsigned long)d);
        }
        canvas_draw_str(canvas, 1, 53, line);
    }
    if(sel != RG_STUDIO_NO_BIT) {
        snprintf(
            line,
            sizeof(line),
            unsure ? "bit %u = ? (unsure)" : "bit %u = %c",
            (unsigned)sel,
            f->bits[sel]);
    } else {
        strlcpy(line, f->nbits ? "no bit (not data)" : "no bits read", sizeof(line));
    }
    canvas_draw_str(canvas, 1, 63, line);
    if(m->marker >= 0 && (uint16_t)m->marker < f->n) {
        uint64_t a = rg_studio_time_at(f, (uint16_t)m->marker);
        uint64_t b = rg_studio_time_at(f, m->cursor);
        uint64_t dt = a > b ? a - b : b - a;
        uint64_t dt_ms = rg_div_u64(dt, 1000u);
        snprintf(
            line,
            sizeof(line),
            "A:%lu.%lums",
            (unsigned long)dt_ms,
            (unsigned long)((uint32_t)(dt - dt_ms * 1000u) / 100u));
        canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, line);
    }
}

/* Lines of text, scrolled; each at most TEXT_CHARS. */
static void studio_draw_lines(Canvas* canvas, StudioModel* m, const char* text) {
    canvas_set_font(canvas, FontSecondary);
    uint16_t line = 0, shown = 0;
    const char* p = text;
    while(*p && shown < TEXT_LINES) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if(line >= m->scroll) {
            char buf[TEXT_CHARS + 2];
            if(len > TEXT_CHARS + 1) len = TEXT_CHARS + 1;
            memcpy(buf, p, len);
            buf[len] = '\0';
            canvas_draw_str(canvas, 1, 19 + shown * 10, buf);
            shown++;
        }
        line++;
        if(!nl) break;
        p = nl + 1;
    }
}

static void studio_draw_frame(Canvas* canvas, StudioModel* m) {
    const StudioFrameInfo* fi = &m->info[m->frame];
    char text[320];
    uint8_t diff[(RG_ANALYZER_MAX_BITS + 7) / 8];
    size_t d = m->ref_n ? rg_studio_diff(&m->f, m->ref, m->ref_n, diff) : 0;
    char group[40] = "no pattern group";
    if(fi->group < RG_ANALYZER_MAX_GROUPS) {
        snprintf(
            group,
            sizeof(group),
            "pattern %c x%u%s",
            'A' + fi->group,
            (unsigned)m->group_exact[fi->group],
            fi->shift ? " shifted" : "");
    }
    snprintf(
        text,
        sizeof(text),
        "Frame %u of %u\n"
        "start %lu.%lu ms\n"
        "%lu us, %u samples\n"
        "%u bits, fit %u%%%s\n"
        "%s\n"
        "vs pattern A: %u differ\n"
        "%s%s"
        "[HYPOTHESIS] reading",
        (unsigned)(m->frame + 1),
        (unsigned)m->frames,
        (unsigned long)rg_div_u64(fi->start_us, 1000u),
        (unsigned long)((uint32_t)(fi->start_us - rg_div_u64(fi->start_us, 1000u) * 1000u) / 100u),
        (unsigned long)fi->duration_us,
        (unsigned)fi->samples,
        (unsigned)fi->bit_count,
        (unsigned)fi->fit,
        fi->fit < RG_ANALYZER_GOOD_FIT ? " (poor)" : "",
        group,
        (unsigned)d,
        fi->truncated ? "truncated (long frame)\n" : "",
        fi->repeat_bits ? "gapless repeats cut\n" : "");
    studio_draw_lines(canvas, m, text);
}

static void studio_draw_bits(Canvas* canvas, StudioModel* m, bool hex, bool diff) {
    const RgStudioFrame* f = &m->f;
    canvas_set_font(canvas, FontKeyboard);
    if(f->nbits == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 1, 30, "No bits: this frame does");
        canvas_draw_str(canvas, 1, 40, "not fit the encoding.");
        return;
    }
    uint8_t mask[(RG_ANALYZER_MAX_BITS + 7) / 8];
    if(diff) rg_studio_diff(f, m->ref, m->ref_n, mask);
    // Bits: 3 groups of 8 a line. Hex: 24 nibbles a line (96 bits).
    uint16_t per_line = hex ? 96u : 24u;
    uint16_t lines = (uint16_t)((f->nbits + per_line - 1u) / per_line);
    for(uint16_t l = 0; l < 5 && l + m->scroll < lines; l++) {
        uint16_t from = (uint16_t)((l + m->scroll) * per_line);
        int32_t y = 19 + l * 10;
        if(hex) {
            char buf[32];
            size_t k = 0;
            for(uint16_t b = from; b < from + per_line && b < f->nbits && k < 26; b += 4) {
                unsigned v = 0, w = 0;
                for(uint16_t j = 0; j < 4 && b + j < f->nbits; j++, w++)
                    v = (v << 1) | (f->bits[b + j] == '1');
                if(w < 4) v <<= (4 - w);
                buf[k++] = "0123456789ABCDEF"[v & 15u];
            }
            buf[k] = '\0';
            canvas_draw_str(canvas, 1, y, buf);
        } else {
            int32_t x = 1;
            for(uint16_t b = from; b < from + per_line && b < f->nbits; b++) {
                char c[2] = {(f->map[b] & RG_BITMAP_UNSURE) ? '?' : f->bits[b], '\0'};
                bool d = diff && (mask[b / 8] & (1u << (b % 8)));
                if(d) {
                    canvas_draw_box(canvas, x - 1, y - 8, 6, 9);
                    canvas_set_color(canvas, ColorWhite);
                }
                canvas_draw_str(canvas, x, y, c);
                canvas_set_color(canvas, ColorBlack);
                x += 5;
                if((b + 1 - from) % 8 == 0) x += 2;
            }
        }
    }
    if(lines > 5) elements_scrollbar_pos(canvas, 128, 11, 52, m->scroll, lines - 4u);
}

static void studio_draw_timing(Canvas* canvas, StudioModel* m) {
    uint8_t hi[RG_STUDIO_HIST], lo[RG_STUDIO_HIST];
    rg_studio_hist(&m->f, hi, lo);
    uint8_t most = 1;
    for(size_t k = 0; k < RG_STUDIO_HIST; k++) {
        if(hi[k] > most) most = hi[k];
        if(lo[k] > most) most = lo[k];
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, 36, 127, 36);
    // Pulses above the line, gaps below, in half-Te bins.
    canvas_draw_str(canvas, 1, 22, "hi");
    canvas_draw_str(canvas, 1, 46, "lo");
    for(size_t k = 0; k < RG_STUDIO_HIST; k++) {
        int32_t x = 14 + (int32_t)k * 7;
        int32_t h = hi[k] * 22 / most, l = lo[k] * 14 / most;
        if(h > 0) canvas_draw_box(canvas, x, 36 - h, 5, (size_t)h);
        if(l > 0) canvas_draw_box(canvas, x, 37, 5, (size_t)l);
    }
    // Te steps: 1, 2, 4 and 8 Te.
    static const uint8_t ticks[] = {1, 3, 7, 15};
    static const char* const labels[] = {"1", "2", "4", "8"};
    canvas_draw_str(canvas, 1, 63, "Te");
    for(size_t i = 0; i < 4; i++)
        canvas_draw_str(canvas, 14 + ticks[i] * 7, 63, labels[i]);
}

static void studio_draw(Canvas* canvas, void* model) {
    StudioModel* m = model;
    canvas_clear(canvas);
    studio_header(canvas, m);
    canvas_set_font(canvas, FontSecondary);
    if(m->frames == 0) {
        canvas_draw_str(canvas, 1, 30, "No frames: nothing here");
        canvas_draw_str(canvas, 1, 40, "decodes as a signal.");
        return;
    }
    if(!m->loaded) {
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignBottom, "Reading frame...");
        return;
    }
    switch(m->page) {
    case StudioPageWave:
        studio_draw_wave(canvas, m);
        break;
    case StudioPageFrame:
        studio_draw_frame(canvas, m);
        break;
    case StudioPageBits:
        studio_draw_bits(canvas, m, false, false);
        break;
    case StudioPageHex:
        studio_draw_bits(canvas, m, true, false);
        break;
    case StudioPageDiff:
        studio_draw_bits(canvas, m, false, true);
        break;
    default:
        studio_draw_timing(canvas, m);
        break;
    }
}

/* ---- input ----------------------------------------------------------------- */

static bool studio_input(InputEvent* event, void* context) {
    RadioGeddonStudioView* instance = context;
    if(event->key == InputKeyBack) return false;
    bool load = false;
    uint8_t frame = 0;
    with_view_model(
        instance->view,
        StudioModel * m,
        {
            bool shortish = event->type == InputTypeShort || event->type == InputTypeRepeat;
            bool lng = event->type == InputTypeLong;
            if(m->frames == 0) {
                // nothing to move
            } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                m->page = (uint8_t)((m->page + 1u) % StudioPageCount);
                m->scroll = 0;
            } else if(event->key == InputKeyOk && lng && m->page == StudioPageWave) {
                m->marker = m->marker == (int32_t)m->cursor ? -1 : (int32_t)m->cursor;
            } else if((event->key == InputKeyUp || event->key == InputKeyDown) && shortish) {
                bool up = event->key == InputKeyUp;
                m->frame = up ? (uint8_t)((m->frame + m->frames - 1u) % m->frames) :
                                (uint8_t)((m->frame + 1u) % m->frames);
                m->loaded = false;
                load = true;
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                bool right = event->key == InputKeyRight;
                if(m->page == StudioPageWave && lng) {
                    if(right && m->zoom > 0) m->zoom--;
                    if(!right && m->zoom + 1 < RG_STUDIO_ZOOMS) m->zoom++;
                } else if(m->page == StudioPageWave && shortish) {
                    if(right && m->cursor + 1u < m->f.n) m->cursor++;
                    if(!right && m->cursor > 0) m->cursor--;
                } else if(shortish) {
                    if(right) m->scroll++;
                    if(!right && m->scroll > 0) m->scroll--;
                    if(m->scroll > 24) m->scroll = 24;
                }
                if(m->page == StudioPageWave)
                    m->view_us = rg_studio_pan(&m->f, m->view_us, m->zoom, WAVE_COLS, m->cursor);
            }
            frame = m->frame;
        },
        true);
    if(load && instance->callback) instance->callback(frame, instance->context);
    return true;
}

/* ---- API ------------------------------------------------------------------- */

RadioGeddonStudioView* radiogeddon_studio_view_alloc(void) {
    RadioGeddonStudioView* instance = malloc(sizeof(RadioGeddonStudioView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(StudioModel));
    view_set_draw_callback(instance->view, studio_draw);
    view_set_input_callback(instance->view, studio_input);
    with_view_model(
        instance->view,
        StudioModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->marker = -1;
        },
        false);
    return instance;
}

void radiogeddon_studio_view_free(RadioGeddonStudioView* instance) {
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_studio_view_get_view(RadioGeddonStudioView* instance) {
    return instance->view;
}

void radiogeddon_studio_view_set_callback(
    RadioGeddonStudioView* instance,
    RadioGeddonStudioCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

size_t radiogeddon_studio_view_memory(void) {
    // The model, and what reading a frame borrows for a moment.
    return sizeof(StudioModel) + sizeof(RadioGeddonStudioView) + sizeof(RgStudioFrame) +
           RG_STUDIO_MAX_SAMPLES * sizeof(int32_t) + 512u;
}

void radiogeddon_studio_view_set_analysis(
    RadioGeddonStudioView* instance,
    const char* name,
    const RgAnalysis* a) {
    with_view_model(
        instance->view,
        StudioModel * m,
        {
            strlcpy(m->name, name ? name : "", sizeof(m->name));
            m->params = a->params;
            m->encoding = (uint8_t)a->encoding;
            m->frames = (uint8_t)(a->frames_kept > RG_ANALYZER_MAX_FRAMES ?
                                      RG_ANALYZER_MAX_FRAMES :
                                      a->frames_kept);
            for(size_t i = 0; i < m->frames; i++) {
                const RgFrame* fr = &a->frames[i];
                StudioFrameInfo* fi = &m->info[i];
                fi->start_us = fr->start_us;
                fi->duration_us = fr->duration_us;
                fi->samples = fr->samples;
                fi->bit_count = fr->bit_count;
                fi->repeat_bits = fr->repeat_bits;
                fi->fit = fr->fit;
                fi->group = fr->group;
                fi->shift = fr->shift;
                fi->truncated = fr->truncated;
            }
            for(size_t g = 0; g < RG_ANALYZER_MAX_GROUPS; g++)
                m->group_exact[g] = g < a->group_count ? a->groups[g].exact : 0;
            strlcpy(m->ref, a->bits, sizeof(m->ref));
            m->ref_n = (uint16_t)strlen(m->ref);
            // Open on the first frame that decodes, if any.
            m->frame = 0;
            for(size_t i = 0; i < m->frames; i++) {
                if(m->info[i].fit >= RG_ANALYZER_GOOD_FIT) {
                    m->frame = (uint8_t)i;
                    break;
                }
            }
            m->loaded = false;
        },
        true);
}

void radiogeddon_studio_view_load_frame(
    RadioGeddonStudioView* instance,
    RgRawReader* reader,
    uint8_t frame) {
    // Copy what is needed, read without the model's lock, then fill it.
    uint64_t start = 0;
    uint32_t duration = 0;
    RgDecodeParams params;
    uint8_t encoding = 0;
    bool valid = false;
    with_view_model(
        instance->view,
        StudioModel * m,
        {
            if(frame < m->frames) {
                start = m->info[frame].start_us;
                duration = m->info[frame].duration_us;
                params = m->params;
                encoding = m->encoding;
                valid = true;
            }
        },
        false);
    if(!valid) return;
    int32_t* buf = malloc(RG_STUDIO_MAX_SAMPLES * sizeof(int32_t));
    size_t n = 0;
    if(rg_raw_reader_seek_time(reader, start)) {
        uint64_t t = reader->time_us;
        int32_t chunk[32];
        size_t k;
        bool done = false;
        while(!done && (k = rg_raw_reader_read(reader, chunk, 32)) > 0) {
            for(size_t i = 0; i < k && !done; i++) {
                uint32_t d = chunk[i] < 0 ? (uint32_t)(-(int64_t)chunk[i]) : (uint32_t)chunk[i];
                if(t >= start) {
                    if(t - start >= duration || n >= RG_STUDIO_MAX_SAMPLES) {
                        done = true;
                        break;
                    }
                    buf[n++] = chunk[i];
                }
                t += d;
            }
        }
    }
    RgStudioFrame* tmp = malloc(sizeof(RgStudioFrame));
    rg_studio_load(tmp, buf, n, (RgEncoding)encoding, &params);
    free(buf);
    with_view_model(
        instance->view,
        StudioModel * m,
        {
            if(m->frame == frame) {
                m->f = *tmp;
                m->loaded = true;
                m->cursor = m->f.preamble < m->f.n ? m->f.preamble : 0;
                m->marker = -1;
                m->zoom = rg_studio_zoom_fit(&m->f, WAVE_COLS);
                // Start close enough to see pulses when the frame is long.
                if(m->zoom > 2) m->zoom = 2;
                m->view_us = rg_studio_pan(&m->f, 0, m->zoom, WAVE_COLS, m->cursor);
            }
        },
        true);
    free(tmp);
}

#endif
