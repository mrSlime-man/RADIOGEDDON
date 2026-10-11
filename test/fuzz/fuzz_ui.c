/**
 * Fuzz target: Dolphin Lab's pure logic (helpers/rg_mascot.c, rg_ui.c).
 *
 * Input: the first byte picks the target.
 *  0: an event stream for the mascot. Each 3 bytes: an event (any value,
 *     unknown ones included), its argument, and how far the clock moves
 *     (sometimes backwards or across the 32-bit wrap); with levels synced
 *     from the bytes too.
 *  1: text wrapped at a width from the input with a fixed-width font.
 *  2: RAW durations for the oscilloscope trace.
 *
 * Invariants (abort on violation):
 *  - every state and pose is in range, and the same input gives the same
 *    frames (deterministic); the mascot never holds more than its fixed state;
 *  - REC shows only while the recorder level is on;
 *  - wrapped lines lie inside the text, in order, and each fits the width
 *    unless it is a single character;
 *  - the trace's span is the sum of the durations and no bit past the columns
 *    is set.
 */
#include "../../helpers/rg_mascot.h"
#include "../../helpers/rg_ui.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

static void mascot_run(const uint8_t* d, size_t n, RgMascotFrame* last, uint32_t* count) {
    RgMascot m;
    uint32_t now = n ? (uint32_t)d[0] << 24 : 0;
    rg_mascot_init(&m, now, 1234);
    bool recording = false;
    *count = 0;
    for(size_t i = 0; i + 2 < n; i += 3) {
        uint8_t op = d[i], arg = d[i + 1], dt = d[i + 2];
        // Mostly forward in small steps; sometimes a jump or a step back.
        if(dt == 0xFF) {
            now += 0x7FFFFFF0u;
        } else if(dt == 0xFE) {
            now -= 500;
        } else {
            now += (uint32_t)dt * 37u;
        }
        if(op >= 0xF0) {
            recording = op & 1;
            rg_mascot_sync(&m, op & 2, op & 4, recording, op & 8, now);
        } else {
            // Unknown values too; the recorder's level comes by sync only,
            // as in the app.
            uint32_t ev = op % (RgMascotEventCount + 3u);
            if(ev == RgMascotEventRecordStarted || ev == RgMascotEventRecordStopped)
                ev = RgMascotEventUserInput;
            rg_mascot_event(&m, (RgMascotEvent)ev, arg, now);
        }
        RgMascotFrame f;
        rg_mascot_frame(&m, now, &f);
        REQUIRE(f.state < RgMascotStateCount);
        REQUIRE(f.eyes < RgMascotEyesCount && f.fin < RgMascotFinCount);
        REQUIRE(f.prop < RgMascotPropCount && f.fx < RgMascotFxCount);
        REQUIRE(f.antenna >= 3 && f.antenna <= 6 && f.body_dy <= 1 && f.phones_lift <= 4);
        REQUIRE(f.progress <= 100 || f.progress == RG_MASCOT_PROGRESS_NONE);
        if(f.fx == RgMascotFxRec) REQUIRE(recording);
        *last = f;
        (*count)++;
    }
}

static uint16_t fixed_width(void* context, const char* text, size_t len) {
    (void)context;
    (void)text;
    return (uint16_t)(len * 5);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 1) return 0;
    const uint8_t* d = data + 1;
    size_t n = size - 1;
    switch(data[0] % 3) {
    case 0: {
        RgMascotFrame a, b;
        uint32_t ca, cb;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        mascot_run(d, n, &a, &ca);
        mascot_run(d, n, &b, &cb);
        REQUIRE(ca == cb && !rg_mascot_frame_changed(&a, &b));
        break;
    }
    case 1: {
        if(n < 1) return 0;
        uint16_t w = d[0];
        char* text = malloc(n);
        memcpy(text, d + 1, n - 1);
        text[n - 1] = '\0';
        size_t len = strlen(text);
        RgUiLine lines[16];
        size_t total = rg_ui_wrap(text, w, fixed_width, NULL, lines, 16);
        REQUIRE(total >= 1);
        size_t end = 0;
        for(size_t i = 0; i < total && i < 16; i++) {
            REQUIRE(lines[i].start >= end || lines[i].len == 0);
            REQUIRE((size_t)lines[i].start + lines[i].len <= len);
            REQUIRE(lines[i].len <= 1 || fixed_width(NULL, text, lines[i].len) <= w);
            if(lines[i].len) end = (size_t)lines[i].start + lines[i].len;
        }
        free(text);
        break;
    }
    default: {
        size_t count = n / 4;
        int32_t* s = calloc(count ? count : 1, sizeof(int32_t));
        uint64_t sum = 0;
        for(size_t i = 0; i < count; i++) {
            memcpy(&s[i], d + 4 * i, 4);
            sum += s[i] < 0 ? (uint64_t)(-(int64_t)s[i]) : (uint64_t)s[i];
        }
        uint8_t bits[12];
        uint64_t span = rg_ui_trace(s, count, 90, bits);
        REQUIRE(span == (count ? sum : 0));
        REQUIRE((bits[11] >> 2) == 0); // 90 columns: bits 90..95 clear
        free(s);
        break;
    }
    }
    return 0;
}
