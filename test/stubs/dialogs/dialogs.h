/* Host stub of the firmware's dialogs service: a message's answer is
 * scripted (stub_dialog_answers), and what was shown is kept for checks. */
#pragma once

#include <stddef.h>

typedef struct DialogsApp DialogsApp;
typedef struct DialogMessage DialogMessage;

#ifndef RG_STUB_ALIGN
#define RG_STUB_ALIGN
typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;
#endif

typedef enum {
    DialogMessageButtonBack,
    DialogMessageButtonLeft,
    DialogMessageButtonCenter,
    DialogMessageButtonRight,
} DialogMessageButton;

DialogMessage* dialog_message_alloc(void);
void dialog_message_free(DialogMessage* message);
void dialog_message_set_header(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);
void dialog_message_set_text(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);
void dialog_message_set_buttons(
    DialogMessage* message,
    const char* left,
    const char* center,
    const char* right);
DialogMessageButton dialog_message_show(DialogsApp* context, const DialogMessage* message);

/* The answers given to the next dialog_message_show calls, in order (then Back). */
extern DialogMessageButton stub_dialog_answers[4];
extern unsigned stub_dialog_shown;
/* The text of the last message shown. */
extern char stub_dialog_text[160];
