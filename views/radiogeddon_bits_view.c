#include "radiogeddon_bits_view.h"

#if RG_FEATURE_BITSTREAM

#include <gui/elements.h>
#include <furi.h>
#include "../helpers/rg_glyph.h"

#define BITS_PER_LINE 24u
#define BITS_X        14 // after a 3-digit position label
#define BITS_TOP      11
#define BITS_LINE_H   7
#define BITS_LINES    6u
#define HEX_PER_ROW   10u
#define HEX_TOP       20
#define HEX_ROWS      5u
#define HEX_STEP      11 // two glyphs and a gap
#define DIFF_TOP      11
#define DIFF_CHUNK_H  13
#define DIFF_CHUNKS   3u
#define FRAME_LINES   5u

static const char* const radiogeddon_bits_mode_names[RadioGeddonBitsModeCount] = {
    "FRAMES",
    "BITS",
    "HEX",
    "DIFF",
    "FIELD",
};

struct RadioGeddonBitsView {
    View* view;
};

typedef struct {
    const RgAnalysis* r;
    uint8_t mode;
    uint16_t frame; // selected frame
    uint16_t ref; // reference frame (frames_kept if none)
    uint16_t frame_top; // first frame line shown
    uint16_t bit; // bit cursor (BITS)
    uint16_t line_top; // first line shown (BITS, DIFF)
    uint8_t align; // byte grid offset (HEX)
    uint16_t byte; // byte cursor (HEX)
    uint16_t f_start; // field (FIELD)
    uint16_t f_end;
    // DIFF markers of the selected frame, rebuilt when it changes.
    char markers[RG_ANALYZER_MAX_BITS + 1];
    uint16_t comparable;
    uint16_t n_const;
    uint16_t n_change;
    uint16_t n_unknown;
} RadioGeddonBitsModel;

static const RgFrame* radiogeddon_bits_sel(const RadioGeddonBitsModel* m) {
    if(!m->r || m->frame >= m->r->frames_kept) return NULL;
    return &m->r->frames[m->frame];
}

static uint16_t radiogeddon_bits_len(const RadioGeddonBitsModel* m) {
    const RgFrame* f = radiogeddon_bits_sel(m);
    if(!f) return 0;
    return f->bit_count > RG_ANALYZER_MAX_BITS ? RG_ANALYZER_MAX_BITS : f->bit_count;
}

/* The selected frame changed: reset the cursors that depend on it. */
static void radiogeddon_bits_select(RadioGeddonBitsModel* m, uint16_t frame) {
    m->frame = frame;
    m->bit = 0;
    m->line_top = 0;
    m->byte = 0;
    uint16_t len = radiogeddon_bits_len(m);
    m->f_start = 0;
    m->f_end = len ? (uint16_t)((len > 8 ? 8 : len) - 1u) : 0;
    size_t c = 0, x = 0, u = 0;
    m->comparable = 0;
    m->markers[0] = '\0';
    if(m->r) m->comparable = (uint16_t)rg_bits_diff(m->r, frame, m->markers, &c, &x, &u);
    m->n_const = (uint16_t)c;
    m->n_change = (uint16_t)x;
    m->n_unknown = (uint16_t)u;
}

static void radiogeddon_bits_glyph(Canvas* canvas, int32_t x, int32_t y, char c, bool inverted) {
    if(inverted) {
        canvas_draw_box(canvas, x - 1, y - 1, RG_GLYPH_W + 2, RG_GLYPH_H + 2);
        canvas_set_color(canvas, ColorWhite);
    }
    for(uint8_t gy = 0; gy < RG_GLYPH_H; gy++)
        for(uint8_t gx = 0; gx < RG_GLYPH_W; gx++)
            if(rg_glyph_pixel(c, gx, gy)) canvas_draw_dot(canvas, x + gx, y + gy);
    if(inverted) canvas_set_color(canvas, ColorBlack);
}

static void radiogeddon_bits_glyphs(Canvas* canvas, int32_t x, int32_t y, const char* s) {
    for(; *s; s++, x += RG_GLYPH_PITCH)
        radiogeddon_bits_glyph(canvas, x, y, *s, false);
}

/* Position label: three digits. */
static void radiogeddon_bits_label(Canvas* canvas, int32_t y, unsigned pos) {
    char text[8];
    snprintf(text, sizeof(text), "%03u", pos % 1000u);
    radiogeddon_bits_glyphs(canvas, 0, y, text);
}

static int32_t radiogeddon_bits_x(uint32_t k) {
    return BITS_X + (int32_t)(k * RG_GLYPH_PITCH + (k / 8u) * 3u);
}

static void radiogeddon_bits_header(Canvas* canvas, const RadioGeddonBitsModel* m) {
    char text[32];
    const RgFrame* f = radiogeddon_bits_sel(m);
    snprintf(
        text,
        sizeof(text),
        "%s F%u/%u",
        radiogeddon_bits_mode_names[m->mode],
        (unsigned)(m->frame + 1u),
        (unsigned)(m->r ? m->r->frames_kept : 0u));
    canvas_draw_str(canvas, 0, 8, text);
    if(f) {
        snprintf(
            text,
            sizeof(text),
            "%c %ub %u%%",
            rg_bits_pattern(m->r, m->frame),
            (unsigned)f->bit_count,
            (unsigned)f->fit);
        canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, text);
    }
}

static void radiogeddon_bits_draw_frames(Canvas* canvas, const RadioGeddonBitsModel* m) {
    const RgAnalysis* r = m->r;
    char text[40];
    // "+" when the capture had more frames than the analyzer keeps.
    snprintf(
        text,
        sizeof(text),
        "FRAMES %u%s",
        (unsigned)r->frames_kept,
        r->frame_count > r->frames_kept ? "+" : "");
    canvas_draw_str(canvas, 0, 8, text);
    snprintf(
        text,
        sizeof(text),
        "%s? c%d",
        r->encoding == RgEncodingManchester ? "Manch" : rg_analyzer_encoding_name(r->encoding),
        r->encoding_confidence);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, text);

    const RgFrame* ref = m->ref < r->frames_kept ? &r->frames[m->ref] : NULL;
    for(uint16_t line = 0; line < FRAME_LINES; line++) {
        uint16_t i = (uint16_t)(m->frame_top + line);
        if(i >= r->frames_kept) break;
        const RgFrame* f = &r->frames[i];
        int32_t y = 17 + line * 9; // footer descenders end on the last row
        uint32_t ms = (uint32_t)(f->start_us / 1000u);
        if(!rg_bits_usable(f)) {
            snprintf(
                text,
                sizeof(text),
                "%u %lu.%03lus noise %u%%",
                (unsigned)(i + 1u),
                (unsigned long)(ms / 1000u),
                (unsigned long)(ms % 1000u),
                (unsigned)f->fit);
        } else {
            int sim = ref ? rg_bits_similarity(ref, f, NULL) : 0;
            snprintf(
                text,
                sizeof(text),
                "%u %lu.%03lus %ub %c x%u %d%%",
                (unsigned)(i + 1u),
                (unsigned long)(ms / 1000u),
                (unsigned long)(ms % 1000u),
                (unsigned)f->bit_count,
                rg_bits_pattern(r, i),
                (unsigned)rg_bits_repeats(r, i),
                sim);
        }
        if(i == m->frame) {
            canvas_draw_box(canvas, 0, y - 8, 128, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 1, y, text);
        canvas_set_color(canvas, ColorBlack);
    }
    // Footer: what the columns mean, and the reference.
    if(ref) {
        snprintf(text, sizeof(text), "HYP: t bits pat xN sim/F%u", (unsigned)(m->ref + 1u));
    } else {
        snprintf(text, sizeof(text), "No clean frame.");
    }
    canvas_draw_str(canvas, 0, 61, text);
}

static void radiogeddon_bits_draw_bits(Canvas* canvas, const RadioGeddonBitsModel* m) {
    const RgFrame* f = radiogeddon_bits_sel(m);
    uint16_t len = radiogeddon_bits_len(m);
    for(uint16_t line = 0; line < BITS_LINES; line++) {
        uint32_t first = (uint32_t)(m->line_top + line) * BITS_PER_LINE;
        if(first >= len) break;
        int32_t y = BITS_TOP + line * BITS_LINE_H;
        radiogeddon_bits_label(canvas, y, first);
        for(uint32_t k = 0; k < BITS_PER_LINE && first + k < len; k++) {
            char c = rg_bits_bit(f, first + k) ? '1' : '0';
            radiogeddon_bits_glyph(canvas, radiogeddon_bits_x(k), y, c, first + k == m->bit);
        }
    }
    char text[40];
    if(len) {
        snprintf(
            text,
            sizeof(text),
            "bit %u = %d   of %u",
            (unsigned)m->bit,
            rg_bits_bit(f, m->bit),
            (unsigned)len);
    } else {
        snprintf(text, sizeof(text), "No bits in this frame.");
    }
    canvas_draw_str(canvas, 0, 61, text);
}

static void radiogeddon_bits_draw_hex(Canvas* canvas, const RadioGeddonBitsModel* m) {
    const RgFrame* f = radiogeddon_bits_sel(m);
    RgBitsBytes layout;
    rg_bits_bytes(f, m->align, &layout);
    char text[48];
    // The bits outside whole bytes, as bits: never padded into a byte.
    char lead[9] = "", tail[9] = "";
    for(size_t k = 0; k < layout.lead; k++)
        lead[k] = rg_bits_bit(f, k) ? '1' : '0';
    lead[layout.lead] = '\0';
    size_t tail_start = layout.lead + layout.bytes * 8u;
    for(size_t k = 0; k < layout.tail; k++)
        tail[k] = rg_bits_bit(f, tail_start + k) ? '1' : '0';
    tail[layout.tail] = '\0';
    snprintf(
        text,
        sizeof(text),
        "+%u lead:%s tail:%s",
        (unsigned)m->align,
        layout.lead ? lead : "-",
        layout.tail ? tail : "-");
    canvas_draw_str(canvas, 0, 17, text);

    for(uint16_t row = 0; row < HEX_ROWS; row++) {
        uint32_t first = (uint32_t)row * HEX_PER_ROW;
        if(first >= layout.bytes) break;
        int32_t y = HEX_TOP + row * BITS_LINE_H;
        radiogeddon_bits_label(canvas, y, (unsigned)(layout.lead + first * 8u));
        for(uint32_t b = 0; b < HEX_PER_ROW && first + b < layout.bytes; b++) {
            uint8_t v = 0;
            rg_bits_byte(f, m->align, first + b, &v);
            bool sel = first + b == m->byte;
            int32_t x = BITS_X + (int32_t)(b * HEX_STEP);
            radiogeddon_bits_glyph(canvas, x, y, "0123456789ABCDEF"[v >> 4], sel);
            radiogeddon_bits_glyph(
                canvas, x + RG_GLYPH_PITCH, y, "0123456789ABCDEF"[v & 0xF], sel);
        }
    }
    if(layout.bytes == 0) {
        canvas_draw_str(canvas, 0, 34, "Fewer than 8 bits after");
        canvas_draw_str(canvas, 0, 43, "the offset: no whole byte.");
    } else {
        uint8_t v = 0;
        rg_bits_byte(f, m->align, m->byte, &v);
        char bin[9];
        for(int k = 0; k < 8; k++)
            bin[k] = (v & (0x80u >> k)) ? '1' : '0';
        bin[8] = '\0';
        snprintf(
            text,
            sizeof(text),
            "B%u @%u %02X=%u %s",
            (unsigned)m->byte,
            (unsigned)(layout.lead + m->byte * 8u),
            v,
            v,
            bin);
        canvas_draw_str(canvas, 0, 61, text);
    }
}

static void radiogeddon_bits_draw_diff(Canvas* canvas, const RadioGeddonBitsModel* m) {
    const RgFrame* f = radiogeddon_bits_sel(m);
    uint16_t len = radiogeddon_bits_len(m);
    for(uint16_t chunk = 0; chunk < DIFF_CHUNKS; chunk++) {
        uint32_t first = (uint32_t)(m->line_top + chunk) * BITS_PER_LINE;
        if(first >= len) break;
        int32_t y = DIFF_TOP + chunk * DIFF_CHUNK_H;
        radiogeddon_bits_label(canvas, y, first);
        for(uint32_t k = 0; k < BITS_PER_LINE && first + k < len; k++) {
            int32_t x = radiogeddon_bits_x(k);
            radiogeddon_bits_glyph(canvas, x, y, rg_bits_bit(f, first + k) ? '1' : '0', false);
            char mk = m->markers[first + k];
            radiogeddon_bits_glyph(
                canvas, x, y + 6, mk ? mk : RG_BITS_UNKNOWN, mk == RG_BITS_CHANGES);
        }
    }
    char text[48];
    if(m->comparable < 2) {
        snprintf(text, sizeof(text), "?: only %u frame(s) comparable", (unsigned)m->comparable);
    } else {
        snprintf(
            text,
            sizeof(text),
            ".%u X%u ?%u over %u frames",
            (unsigned)m->n_const,
            (unsigned)m->n_change,
            (unsigned)m->n_unknown,
            (unsigned)m->comparable);
    }
    canvas_draw_str(canvas, 0, 61, text);
}

static void radiogeddon_bits_draw_field(Canvas* canvas, const RadioGeddonBitsModel* m) {
    const RgFrame* f = radiogeddon_bits_sel(m);
    uint16_t len = radiogeddon_bits_len(m);
    char text[48];
    RgBitsField fld;
    if(rg_bits_field(f, m->f_start, m->f_end, &fld) != RgBitsFieldOk) {
        canvas_draw_str(canvas, 0, 32, "No bits to select.");
        return;
    }
    // A 24-bit window holding the start, field bits inverted.
    uint32_t first = (m->f_start / BITS_PER_LINE) * BITS_PER_LINE;
    radiogeddon_bits_label(canvas, 12, first);
    for(uint32_t k = 0; k < BITS_PER_LINE && first + k < len; k++) {
        uint32_t i = first + k;
        bool in = i >= fld.start && i <= fld.end;
        radiogeddon_bits_glyph(
            canvas, radiogeddon_bits_x(k), 12, rg_bits_bit(f, i) ? '1' : '0', in);
    }
    snprintf(
        text,
        sizeof(text),
        "Bits %u-%u, %u bit%s",
        (unsigned)fld.start,
        (unsigned)fld.end,
        (unsigned)fld.length,
        fld.length == 1 ? "" : "s");
    canvas_draw_str(canvas, 0, 25, text);
    char bin[26];
    rg_bits_field_binary(f, &fld, bin, sizeof(bin));
    snprintf(text, sizeof(text), "BIN %s", bin);
    canvas_draw_str(canvas, 0, 34, text);
    if(fld.has_value) {
        char hex[20];
        rg_bits_field_hex(&fld, hex, sizeof(hex));
        snprintf(text, sizeof(text), "HEX %s", hex);
        canvas_draw_str(canvas, 0, 43, text);
        // Up to 15 digits fit the line; a longer field reads in hex.
        if(fld.length <= 48) {
            snprintf(text, sizeof(text), "DEC %llu", (unsigned long long)fld.value);
        } else {
            snprintf(text, sizeof(text), "DEC: over 48 bits, see HEX");
        }
        canvas_draw_str(canvas, 0, 52, text);
    } else {
        canvas_draw_str(canvas, 0, 43, "Over 64 bits: no number.");
    }
    canvas_draw_str(canvas, 0, 61, "Up/Dn start  L/R end");
}

static void radiogeddon_bits_view_draw(Canvas* canvas, void* model) {
    RadioGeddonBitsModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    if(!m->r || m->r->frames_kept == 0) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "No frames decoded.");
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "Try Unknown Protocol");
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, "Analysis for timing.");
        return;
    }
    if(m->mode == RadioGeddonBitsModeFrames) {
        radiogeddon_bits_draw_frames(canvas, m);
        return;
    }
    radiogeddon_bits_header(canvas, m);
    switch(m->mode) {
    case RadioGeddonBitsModeBits:
        radiogeddon_bits_draw_bits(canvas, m);
        break;
    case RadioGeddonBitsModeHex:
        radiogeddon_bits_draw_hex(canvas, m);
        break;
    case RadioGeddonBitsModeDiff:
        radiogeddon_bits_draw_diff(canvas, m);
        break;
    default:
        radiogeddon_bits_draw_field(canvas, m);
        break;
    }
}

/* Keep the bit cursor's line in view (BITS: 6 lines). */
static void radiogeddon_bits_follow(RadioGeddonBitsModel* m, uint16_t lines) {
    uint16_t line = (uint16_t)(m->bit / BITS_PER_LINE);
    if(line < m->line_top) m->line_top = line;
    if(line >= m->line_top + lines) m->line_top = (uint16_t)(line - lines + 1u);
}

static void radiogeddon_bits_input_model(RadioGeddonBitsModel* m, InputEvent* event) {
    bool press = event->type == InputTypeShort || event->type == InputTypeRepeat ||
                 event->type == InputTypeLong;
    uint16_t len = radiogeddon_bits_len(m);
    uint16_t frames = m->r ? (uint16_t)m->r->frames_kept : 0;
    if(event->key == InputKeyOk) {
        if(event->type == InputTypeShort) {
            m->mode = (uint8_t)((m->mode + 1u) % RadioGeddonBitsModeCount);
        } else if(event->type == InputTypeLong) {
            if(m->mode == RadioGeddonBitsModeBits && len) {
                // Start a field at the cursor: one byte, or what is left.
                m->f_start = m->bit;
                m->f_end = (uint16_t)(m->bit + 7u < len ? m->bit + 7u : len - 1u);
                m->mode = RadioGeddonBitsModeField;
            } else if(m->mode == RadioGeddonBitsModeHex) {
                m->align = (uint8_t)((m->align + 1u) % 8u);
                m->byte = 0;
            }
        }
        return;
    }
    if(!press) return;
    bool up = event->key == InputKeyUp, down = event->key == InputKeyDown;
    bool left = event->key == InputKeyLeft, right = event->key == InputKeyRight;
    switch(m->mode) {
    case RadioGeddonBitsModeFrames:
        if((up || down) && frames) {
            uint16_t f = up ? (m->frame ? m->frame - 1u : frames - 1u) :
                              (m->frame + 1u < frames ? m->frame + 1u : 0);
            radiogeddon_bits_select(m, f);
            if(m->frame < m->frame_top) m->frame_top = m->frame;
            if(m->frame >= m->frame_top + FRAME_LINES)
                m->frame_top = (uint16_t)(m->frame - FRAME_LINES + 1u);
        }
        break;
    case RadioGeddonBitsModeBits:
        if(!len) break;
        if(left) m->bit = m->bit ? m->bit - 1u : len - 1u;
        if(right) m->bit = m->bit + 1u < len ? m->bit + 1u : 0;
        if(up) m->bit = m->bit >= BITS_PER_LINE ? (uint16_t)(m->bit - BITS_PER_LINE) : m->bit;
        if(down && m->bit + BITS_PER_LINE < len) m->bit = (uint16_t)(m->bit + BITS_PER_LINE);
        radiogeddon_bits_follow(m, BITS_LINES);
        break;
    case RadioGeddonBitsModeHex: {
        RgBitsBytes layout;
        rg_bits_bytes(radiogeddon_bits_sel(m), m->align, &layout);
        uint16_t n = (uint16_t)layout.bytes;
        if(!n) break;
        if(left) m->byte = m->byte ? m->byte - 1u : n - 1u;
        if(right) m->byte = m->byte + 1u < n ? m->byte + 1u : 0;
        if(up && m->byte >= HEX_PER_ROW) m->byte = (uint16_t)(m->byte - HEX_PER_ROW);
        if(down && m->byte + HEX_PER_ROW < n) m->byte = (uint16_t)(m->byte + HEX_PER_ROW);
        break;
    }
    case RadioGeddonBitsModeDiff: {
        uint16_t lines = (uint16_t)((len + BITS_PER_LINE - 1u) / BITS_PER_LINE);
        if(down && m->line_top + DIFF_CHUNKS < lines) m->line_top++;
        if(up && m->line_top) m->line_top--;
        break;
    }
    default: // field
        if(!len) break;
        if(up && m->f_start) m->f_start--;
        if(down && m->f_start < m->f_end) m->f_start++;
        if(left && m->f_end > m->f_start) m->f_end--;
        if(right && m->f_end + 1u < len) m->f_end++;
        break;
    }
}

static bool radiogeddon_bits_view_input(InputEvent* event, void* context) {
    RadioGeddonBitsView* instance = context;
    if(event->key == InputKeyBack) return false;
    with_view_model(
        instance->view,
        RadioGeddonBitsModel * m,
        { radiogeddon_bits_input_model(m, event); },
        true);
    return true;
}

RadioGeddonBitsView* radiogeddon_bits_view_alloc(void) {
    RadioGeddonBitsView* instance = malloc(sizeof(RadioGeddonBitsView));
    instance->view = view_alloc();
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonBitsModel));
    view_set_draw_callback(instance->view, radiogeddon_bits_view_draw);
    view_set_input_callback(instance->view, radiogeddon_bits_view_input);
    with_view_model(
        instance->view, RadioGeddonBitsModel * m, { memset(m, 0, sizeof(*m)); }, false);
    return instance;
}

void radiogeddon_bits_view_free(RadioGeddonBitsView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_bits_view_get_view(RadioGeddonBitsView* instance) {
    return instance->view;
}

static void radiogeddon_bits_set(RadioGeddonBitsModel* m, const RgAnalysis* analysis) {
    memset(m, 0, sizeof(*m));
    m->r = analysis;
    m->ref = analysis ? (uint16_t)rg_bits_reference(analysis) : 0;
    uint16_t start = (analysis && m->ref < analysis->frames_kept) ? m->ref : 0;
    radiogeddon_bits_select(m, start);
    m->frame_top = start >= FRAME_LINES ? (uint16_t)(start - FRAME_LINES + 1u) : 0;
}

void radiogeddon_bits_view_set_analysis(RadioGeddonBitsView* instance, const RgAnalysis* analysis) {
    with_view_model(
        instance->view, RadioGeddonBitsModel * m, { radiogeddon_bits_set(m, analysis); }, true);
}

void radiogeddon_bits_view_set_mode(RadioGeddonBitsView* instance, RadioGeddonBitsMode mode) {
    with_view_model(
        instance->view,
        RadioGeddonBitsModel * m,
        { m->mode = (uint8_t)(mode < RadioGeddonBitsModeCount ? mode : 0); },
        true);
}

RadioGeddonBitsMode radiogeddon_bits_view_get_mode(RadioGeddonBitsView* instance) {
    RadioGeddonBitsMode mode = RadioGeddonBitsModeFrames;
    with_view_model(
        instance->view, RadioGeddonBitsModel * m, { mode = (RadioGeddonBitsMode)m->mode; }, false);
    return mode;
}

#endif
