/**
 * @file radiogeddon_lab_view.h
 * @brief Dolphin Lab (Full edition): the dashboard home screen and the
 * cards that replace popups in the Dolphin Lab and Cyber Lab styles.
 *
 * Home: the lab scene with the animated dolphin on the left (Dolphin Lab)
 * or a radar and technical readouts (Cyber Lab), the main menu on the right
 * and a status bar. Keys: Up/Down move (wrapping), Left/Right a page, OK
 * opens the item, long OK its shortcut; Back is left to the scene.
 *
 * Cards: a message (error or notice), a busy or progress screen for long
 * work (the percentage only when the work reports one), a save result and
 * an analysis reveal; each says what the popup it replaces said, word
 * wrapped, with the dolphin beside it when the text fits that way and
 * without it when it does not. The text is never shortened.
 *
 * The view keeps no timer: the app's 100 ms tick calls
 * radiogeddon_lab_view_update(), which redraws only when the frame changed
 * and only while the view is on screen. Exists only while the style is
 * not Classic.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <gui/view.h>
#include "../helpers/rg_mascot.h"
#include "../helpers/rg_ui.h"
#include "../helpers/rg_sprites.h"

typedef struct RadioGeddonLabView RadioGeddonLabView;

/** A dashboard item: its label and its 7x7 icon. */
typedef struct {
    const char* label;
    uint8_t icon; // RgSpriteId
} RadioGeddonLabItem;

#define RADIOGEDDON_LAB_MAX_ITEMS 10
#define RADIOGEDDON_LAB_TEXT_MAX  200

typedef enum {
    RadioGeddonLabCardMessage, // an error or notice; OK or Back closes it
    RadioGeddonLabCardBusy, // long work without progress figures
    RadioGeddonLabCardProgress, // long work reporting a percentage
    RadioGeddonLabCardSaved, // a save result
    RadioGeddonLabCardReveal, // an analysis result being revealed
    RadioGeddonLabCardCount,
} RadioGeddonLabCard;

typedef enum {
    RadioGeddonLabEventSelect, // OK on the home item `index`
    RadioGeddonLabEventShortcut, // long OK on the home item `index`
    RadioGeddonLabEventKey, // any key (the mascot wakes up)
    RadioGeddonLabEventDismiss, // OK on a card that can be closed
} RadioGeddonLabEvent;

typedef void (*RadioGeddonLabCallback)(RadioGeddonLabEvent event, uint32_t index, void* context);

RadioGeddonLabView* radiogeddon_lab_view_alloc(void);
void radiogeddon_lab_view_free(RadioGeddonLabView* instance);
View* radiogeddon_lab_view_get_view(RadioGeddonLabView* instance);

void radiogeddon_lab_view_set_callback(
    RadioGeddonLabView* instance,
    RadioGeddonLabCallback callback,
    void* context);

/** True while the view is the one on screen (its enter/exit callbacks). */
bool radiogeddon_lab_view_is_visible(RadioGeddonLabView* instance);

/** Dolphin Lab or Cyber Lab. */
void radiogeddon_lab_view_set_style(RadioGeddonLabView* instance, RgUiStyle style);

/** Show the dashboard with @p count items (kept by pointer), @p selected highlighted. */
void radiogeddon_lab_view_show_home(
    RadioGeddonLabView* instance,
    const RadioGeddonLabItem* items,
    uint8_t count,
    uint8_t selected);
uint8_t radiogeddon_lab_view_get_selected(RadioGeddonLabView* instance);

/** Status bar: @p left (frequency, preset, radio) and @p right (session,
 * warning or edition), copied; @p warn draws the right part inverted. */
void radiogeddon_lab_view_set_status(
    RadioGeddonLabView* instance,
    const char* left,
    const char* right,
    bool warn);

/** The decoration on the lab wall (RgDecoration, 0: none). */
void radiogeddon_lab_view_set_decoration(RadioGeddonLabView* instance, uint8_t decoration);

/** Cyber Lab readout line (free heap and the like), copied. */
void radiogeddon_lab_view_set_readout(RadioGeddonLabView* instance, const char* text);

/** Show a card; @p title and @p text are copied (text cut at
 * RADIOGEDDON_LAB_TEXT_MAX - 1 characters). */
void radiogeddon_lab_view_show_card(
    RadioGeddonLabView* instance,
    RadioGeddonLabCard card,
    const char* title,
    const char* text);

/** The percentage of a progress card (RG_MASCOT_PROGRESS_NONE: unknown). */
void radiogeddon_lab_view_set_progress(RadioGeddonLabView* instance, uint8_t percent);

/** Start a transition into what is shown now. */
void radiogeddon_lab_view_transition(
    RadioGeddonLabView* instance,
    RgTransitionKind kind,
    uint16_t duration_ms,
    uint32_t now);

/** True while a transition runs. */
bool radiogeddon_lab_view_transition_active(RadioGeddonLabView* instance);

/**
 * Advance to @p now with the mascot's @p frame (may be NULL): redraws only if
 * something visible changed. Call on the app tick.
 */
void radiogeddon_lab_view_update(
    RadioGeddonLabView* instance,
    const RgMascotFrame* frame,
    uint32_t now);
