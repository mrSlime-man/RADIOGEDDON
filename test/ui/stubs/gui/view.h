/**
 * Host stand-in for the firmware's gui/view.h and input types (test/ui): a
 * view keeps its callbacks and model so a test can draw it into the host
 * canvas and send it key events, as the GUI thread would.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <gui/canvas.h>

typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
    InputKeyMAX,
} InputKey;

typedef enum {
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat,
    InputTypeMAX,
} InputType;

typedef struct {
    uint32_t sequence;
    InputKey key;
    InputType type;
} InputEvent;

typedef void (*ViewDrawCallback)(Canvas* canvas, void* model);
typedef bool (*ViewInputCallback)(InputEvent* event, void* context);

typedef enum {
    ViewModelTypeNone,
    ViewModelTypeLockFree,
    ViewModelTypeLocking,
} ViewModelType;

typedef struct View View;

View* view_alloc(void);
void view_free(View* view);
void view_set_context(View* view, void* context);
void view_allocate_model(View* view, ViewModelType type, size_t size);
void view_set_draw_callback(View* view, ViewDrawCallback callback);
void view_set_input_callback(View* view, ViewInputCallback callback);
void* view_get_model(View* view);
void view_commit_model(View* view, bool update);

#define with_view_model(view, type, code, update) \
    {                                             \
        type = view_get_model(view);              \
        {code};                                   \
        view_commit_model(view, update);          \
    }

/* Host harness: draw the view into @p canvas / send it a key. */
void view_host_draw(View* view, Canvas* canvas);
bool view_host_input(View* view, InputKey key, InputType type);
