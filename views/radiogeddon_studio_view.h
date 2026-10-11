/**
 * @file radiogeddon_studio_view.h
 * @brief Pulse Studio (Full edition, a module): one RAW capture's frames,
 * waveform, bits and timing on one screen.
 *
 * Pages (OK cycles): Wave (the frame's pulses with zoom, pan, a cursor on a
 * pulse or gap, a measuring marker, preamble, glitch and frame-end marks,
 * and the bit each symbol decodes to), Frame (the frame inspector), Bits,
 * Hex, Diff (against the most common pattern) and Timing (the frame's
 * pulse and gap histogram in half-Te steps). Up/Down change frame,
 * Left/Right move the cursor (or scroll), long Left/Right zoom out/in, long
 * OK sets or clears the marker. Back leaves.
 *
 * The bits are the analyzer's own reading (helpers/rg_studio.h): a pulse
 * that belongs to no bit says so, and a symbol that did not fit the grammar
 * shows its bit as '?'. Samples are read from the file one frame at a time
 * (radiogeddon_studio_view_load_frame, on the app thread when the view asks
 * through its callback); a long recording is never held in memory.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <gui/view.h>
#include "../helpers/rg_analyzer.h"
#include "../helpers/rg_raw.h"

typedef struct RadioGeddonStudioView RadioGeddonStudioView;

/** The view wants frame @p frame's samples (call load_frame from the app thread). */
typedef void (*RadioGeddonStudioCallback)(uint8_t frame, void* context);

RadioGeddonStudioView* radiogeddon_studio_view_alloc(void);
void radiogeddon_studio_view_free(RadioGeddonStudioView* instance);
View* radiogeddon_studio_view_get_view(RadioGeddonStudioView* instance);
void radiogeddon_studio_view_set_callback(
    RadioGeddonStudioView* instance,
    RadioGeddonStudioCallback callback,
    void* context);

/** Copy what the screen needs from @p analysis (frames, timing, patterns). */
void radiogeddon_studio_view_set_analysis(
    RadioGeddonStudioView* instance,
    const char* name,
    const RgAnalysis* analysis);

/**
 * Read frame @p frame's samples through @p reader (seeking by time) and show
 * it. The reader belongs to the caller and stays open while the view lives.
 */
void radiogeddon_studio_view_load_frame(
    RadioGeddonStudioView* instance,
    RgRawReader* reader,
    uint8_t frame);

/** Heap the view takes (its model), for the module's memory check. */
size_t radiogeddon_studio_view_memory(void);
