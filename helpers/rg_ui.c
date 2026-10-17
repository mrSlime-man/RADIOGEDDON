#include "rg_ui.h"
#include "rg_div.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_DOLPHIN_LAB

#include <string.h>

/* ---- styles -------------------------------------------------------------- */

const char* rg_ui_style_name(RgUiStyle style) {
    static const char* const names[RgUiStyleCount] = {"Dolphin Lab", "Cyber Lab", "Classic"};
    return (unsigned)style < RgUiStyleCount ? names[style] : "?";
}

const char* rg_ui_feedback_name(RgUiFeedbackLevel level) {
    static const char* const names[RgUiFeedbackCount] = {"Off", "Minimal", "Full"};
    return (unsigned)level < RgUiFeedbackCount ? names[level] : "?";
}

RgUiStyle rg_ui_style_effective(uint32_t saved, uint32_t free_heap) {
    if(saved >= RgUiStyleCount) saved = RgUiStyleClassic;
    if(free_heap < RG_UI_MIN_FREE_HEAP) return RgUiStyleClassic;
    return (RgUiStyle)saved;
}

/* ---- feedback -------------------------------------------------------------- */

void rg_ui_feedback_init(RgUiFeedback* fb) {
    memset(fb, 0, sizeof(*fb));
}

static bool rg_ui_since(uint8_t played, uint32_t last, uint32_t now, uint32_t min_ms) {
    return !played || now - last >= min_ms;
}

RgUiCue
    rg_ui_feedback(RgUiFeedback* fb, RgUiFeedbackLevel level, RgUiFbEvent event, uint32_t now) {
    if((unsigned)event >= RgUiFbCount) return RgUiCueNone;
    RgUiCue cue = RgUiCueNone;
    uint32_t min_ms = RG_UI_FB_OTHER_MS;
    // A save and an error already have the app's success / error sequence;
    // nothing is added on top of them at any level.
    switch(event) {
    case RgUiFbMenu:
        if(level == RgUiFeedbackFull) cue = RgUiCueClick;
        min_ms = RG_UI_FB_MENU_MS;
        break;
    case RgUiFbSignal:
        if(level == RgUiFeedbackFull) cue = RgUiCueBlip;
        min_ms = RG_UI_FB_SIGNAL_MS;
        break;
    case RgUiFbRecordStart:
        if(level == RgUiFeedbackMinimal) cue = RgUiCueTick;
        if(level == RgUiFeedbackFull) cue = RgUiCueRecord;
        break;
    case RgUiFbAnalysisDone:
        if(level == RgUiFeedbackMinimal) cue = RgUiCueTick;
        if(level == RgUiFeedbackFull) cue = RgUiCueChime;
        break;
    case RgUiFbRecordSaved:
    case RgUiFbError:
    default:
        break;
    }
    if(cue == RgUiCueNone) return RgUiCueNone;
    if(!rg_ui_since(fb->played[event], fb->last[event], now, min_ms)) return RgUiCueNone;
    if(!rg_ui_since(fb->played_any, fb->last_any, now, RG_UI_FB_ANY_MS)) return RgUiCueNone;
    fb->played[event] = 1;
    fb->last[event] = now;
    fb->played_any = 1;
    fb->last_any = now;
    return cue;
}

/* ---- transitions ----------------------------------------------------------- */

void rg_transition_start(
    RgTransition* t,
    RgTransitionKind kind,
    uint16_t duration_ms,
    uint32_t now) {
    t->start = now;
    t->duration_ms = duration_ms ? duration_ms : 1;
    t->kind = (uint8_t)kind;
    t->active = kind != RgTransitionNone && (unsigned)kind < RgTransitionCount;
}

void rg_transition_finish(RgTransition* t) {
    t->active = 0;
}

uint8_t rg_transition_level(RgTransition* t, uint32_t now) {
    if(!t->active) return RG_TRANSITION_DONE;
    uint32_t elapsed = now - t->start;
    if((int32_t)elapsed < 0) elapsed = 0;
    if(elapsed >= t->duration_ms) {
        t->active = 0;
        return RG_TRANSITION_DONE;
    }
    uint32_t level = elapsed * 255u / t->duration_ms;
    return (uint8_t)(level > 254u ? 254u : level);
}

bool rg_dither_hidden(uint32_t x, uint32_t y, uint8_t level) {
    static const uint8_t bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    // Threshold 8..248: level 0 hides every pixel, 255 none.
    return (uint32_t)bayer[y & 3u][x & 3u] * 16u + 8u > level;
}

/* ---- menu ------------------------------------------------------------------ */

uint8_t rg_ui_menu_top(uint8_t selected, uint8_t top, uint8_t count, uint8_t rows) {
    if(count == 0 || rows == 0) return 0;
    if(selected >= count) selected = (uint8_t)(count - 1);
    if(selected < top) top = selected;
    if(selected >= top + rows) top = (uint8_t)(selected - rows + 1);
    if(count > rows && top > count - rows) top = (uint8_t)(count - rows);
    if(count <= rows) top = 0;
    return top;
}

/* ---- text ------------------------------------------------------------------ */

static void rg_ui_wrap_add(RgUiLine* lines, size_t max, size_t n, size_t start, size_t len) {
    if(n < max) {
        lines[n].start = (uint16_t)start;
        lines[n].len = (uint16_t)len;
    }
}

size_t rg_ui_wrap(
    const char* text,
    uint16_t max_w,
    RgUiWidthFn width,
    void* context,
    RgUiLine* lines,
    size_t max_lines) {
    size_t n = 0;
    size_t total = strlen(text);
    if(total > UINT16_MAX) total = UINT16_MAX;
    size_t pos = 0;
    while(pos <= total) {
        // One paragraph: up to the next line break or the end.
        size_t end = pos;
        while(end < total && text[end] != '\n')
            end++;
        size_t line = pos;
        if(line == end) {
            rg_ui_wrap_add(lines, max_lines, n++, line, 0);
        }
        while(line < end) {
            // Longest run of whole words from `line` that fits.
            size_t fit = line;
            size_t i = line;
            while(i < end) {
                size_t w_end = i;
                while(w_end < end && text[w_end] != ' ')
                    w_end++;
                if(width(context, text + line, w_end - line) > max_w) break;
                fit = w_end;
                i = w_end;
                while(i < end && text[i] == ' ')
                    i++;
            }
            if(fit == line) {
                // One word wider than the line: as many characters as fit.
                fit = line + 1;
                while(fit < end && text[fit] != ' ' &&
                      width(context, text + line, fit + 1 - line) <= max_w)
                    fit++;
            }
            rg_ui_wrap_add(lines, max_lines, n++, line, fit - line);
            line = fit;
            while(line < end && text[line] == ' ')
                line++;
        }
        if(end >= total) break;
        pos = end + 1;
    }
    return n;
}

bool rg_ui_message_is_notice(const char* header) {
    if(!header || strncmp(header, "Not ", 4) == 0 || strncmp(header, "Already", 7) == 0)
        return false;
    return strstr(header, "saved") != NULL || strstr(header, "created") != NULL;
}

/* ---- oscilloscope trace ---------------------------------------------------- */

static uint64_t rg_ui_abs(int32_t v) {
    return v < 0 ? (uint64_t)(-(int64_t)v) : (uint64_t)v;
}

uint64_t rg_ui_trace(const int32_t* samples, size_t n, uint32_t cols, uint8_t* bits) {
    memset(bits, 0, (cols + 7u) / 8u);
    uint64_t span = 0;
    for(size_t i = 0; i < n; i++)
        span += rg_ui_abs(samples[i]);
    if(n == 0 || span == 0 || cols == 0) return 0;
    size_t i = 0;
    uint64_t t_end = rg_ui_abs(samples[0]);
    for(uint32_t c = 0; c < cols; c++) {
        uint64_t t = rg_div_u64(span * (2u * (uint64_t)c + 1u), 2u * (uint64_t)cols);
        while(t >= t_end && i + 1 < n) {
            i++;
            t_end += rg_ui_abs(samples[i]);
        }
        if(samples[i] > 0) bits[c / 8u] |= (uint8_t)(1u << (c % 8u));
    }
    return span;
}

/* ---- firmware -------------------------------------------------------------- */

const char* rg_ui_fw_family(const char* version) {
    if(!version || !version[0]) return "Other";
    if(strncmp(version, "mntm", 4) == 0) return "Momentum";
    if(strncmp(version, "unlshd", 6) == 0) return "Unleashed";
    if((version[0] == 'R' || version[0] == 'r') && (version[1] == 'M' || version[1] == 'm'))
        return "RogueMaster";
    if(version[0] >= '0' && version[0] <= '9') return "Official";
    return "Other";
}

#endif
