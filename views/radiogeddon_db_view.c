#include "radiogeddon_db_view.h"
#include <gui/elements.h>
#include <furi.h>

#define DB_VIEW_ROWS     4
#define DB_VIEW_ROW_H    10
#define DB_VIEW_LIST_TOP 11
#define DB_VIEW_TEXT_W   121 /* scroll bar to the right of this */

typedef struct {
    RadioGeddonDb* db;
    size_t selected;
    size_t top;
} RadioGeddonDbModel;

struct RadioGeddonDbView {
    View* view;
    RadioGeddonDbViewCallback callback;
    void* context;
    RadioGeddonDbModel* locked; /* between lock and unlock */
};

/* Short type tag shown at the right of a row. */
static void radiogeddon_db_view_tag(const RgDbEntry* e, char* out, size_t size) {
    const char* t;
    char proto[8];
    switch(e->kind) {
    case RgDbKindCorrupt:
        t = "BAD";
        break;
    case RgDbKindUnknown:
        t = "?";
        break;
    case RgDbKindRaw:
        t = "RAW";
        break;
    default:
        strncpy(proto, e->protocol, sizeof(proto) - 1);
        proto[sizeof(proto) - 1] = '\0';
        t = proto;
        break;
    }
    snprintf(out, size, "%s%s", (e->flags & RG_DB_FLAG_DUPLICATE) ? "=" : "", t);
}

/* Copy @p text into @p out, shortened until it fits @p width pixels. */
static void radiogeddon_db_view_fit(
    Canvas* canvas,
    const char* text,
    size_t text_len,
    char* out,
    size_t size,
    uint16_t width) {
    size_t n = text_len < size - 1 ? text_len : size - 1;
    memcpy(out, text, n);
    out[n] = '\0';
    while(n > 0 && canvas_string_width(canvas, out) > width) {
        out[--n] = '\0';
    }
}

static void radiogeddon_db_view_header(Canvas* canvas, const RadioGeddonDb* db) {
    char left[24];
    const RgDbQuery* q = &db->query;
    const char* what = q->show == RgDbShowAll      ? "Signals" :
                       q->show == RgDbShowProtocol ? q->protocol :
                                                     rg_db_show_name(q->show);
    snprintf(left, sizeof(left), "%s%s", q->search[0] ? "*" : "", what);
    char right[24];
    snprintf(
        right,
        sizeof(right),
        "%s %u/%u",
        rg_db_sort_name(q->sort),
        (unsigned)db->view_count,
        (unsigned)db->total_files);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, right);
    // The filter name gets whatever room the counts leave.
    char shown[24];
    uint16_t room = 127 - canvas_string_width(canvas, right) - 4;
    radiogeddon_db_view_fit(canvas, left, strlen(left), shown, sizeof(shown), room);
    canvas_draw_str(canvas, 1, 8, shown);
    canvas_draw_line(canvas, 0, 10, 127, 10);
}

static void radiogeddon_db_view_footer(Canvas* canvas, const RgDbEntry* e) {
    canvas_draw_line(canvas, 0, 52, 127, 52);
    if(!e) {
        canvas_draw_str(canvas, 1, 62, "Right: options");
        return;
    }
    char line[40];
    if(e->kind == RgDbKindCorrupt) {
        snprintf(line, sizeof(line), "Not a readable .sub file");
    } else {
        char when[16];
        radiogeddon_db_format_time(e->mtime, when, sizeof(when));
        char freq[12];
        if(e->frequency) {
            snprintf(
                freq,
                sizeof(freq),
                "%lu.%02lu",
                (unsigned long)(e->frequency / 1000000),
                (unsigned long)((e->frequency % 1000000) / 10000));
        } else {
            snprintf(freq, sizeof(freq), "-");
        }
        if(e->dup_count) {
            snprintf(line, sizeof(line), "%s %s  =%u", freq, when, (unsigned)e->dup_count);
        } else {
            snprintf(line, sizeof(line), "%s  %s", freq, when);
        }
    }
    canvas_draw_str(canvas, 1, 62, line);
}

static void radiogeddon_db_view_draw(Canvas* canvas, void* model) {
    RadioGeddonDbModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    RadioGeddonDb* db = m->db;
    if(!db) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Loading...");
        return;
    }
    radiogeddon_db_view_header(canvas, db);

    if(db->view_count == 0) {
        const char* msg = db->total_files == 0 ? "No recordings yet" : "No matches";
        canvas_draw_str_aligned(canvas, 64, 31, AlignCenter, AlignCenter, msg);
        radiogeddon_db_view_footer(canvas, NULL);
        return;
    }

    for(size_t r = 0; r < DB_VIEW_ROWS; r++) {
        size_t pos = m->top + r;
        const RgDbEntry* e = radiogeddon_db_at(db, pos);
        if(!e) break;
        int32_t y = DB_VIEW_LIST_TOP + (int32_t)(r * DB_VIEW_ROW_H);
        bool sel = pos == m->selected;
        if(sel) {
            canvas_draw_box(canvas, 0, y, DB_VIEW_TEXT_W + 2, DB_VIEW_ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }
        char tag[12];
        radiogeddon_db_view_tag(e, tag, sizeof(tag));
        uint16_t tag_w = canvas_string_width(canvas, tag);
        canvas_draw_str_aligned(canvas, DB_VIEW_TEXT_W, y + 8, AlignRight, AlignBottom, tag);

        const char* name = rg_db_name(&db->db, e);
        size_t len = strlen(name);
        if(len > 4 && strcmp(name + len - 4, ".sub") == 0) len -= 4;
        char shown[40];
        uint16_t room = DB_VIEW_TEXT_W - tag_w - 6;
        radiogeddon_db_view_fit(canvas, name, len, shown, sizeof(shown), room);
        canvas_draw_str(canvas, 2, y + 8, shown);
        if(sel) canvas_set_color(canvas, ColorBlack);
    }
    elements_scrollbar_pos(
        canvas, 127, DB_VIEW_LIST_TOP, DB_VIEW_ROWS * DB_VIEW_ROW_H, m->selected, db->view_count);
    radiogeddon_db_view_footer(canvas, radiogeddon_db_at(db, m->selected));
}

static void radiogeddon_db_view_keep_visible(RadioGeddonDbModel* m) {
    if(m->selected < m->top) m->top = m->selected;
    if(m->selected >= m->top + DB_VIEW_ROWS) m->top = m->selected - DB_VIEW_ROWS + 1;
}

static bool radiogeddon_db_view_input(InputEvent* event, void* context) {
    RadioGeddonDbView* instance = context;
    bool consumed = false;
    RadioGeddonDbViewEvent out;
    bool fire = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp || event->key == InputKeyDown) {
            bool up = event->key == InputKeyUp;
            with_view_model(
                instance->view,
                RadioGeddonDbModel * m,
                {
                    size_t n = m->db ? m->db->view_count : 0;
                    if(n > 0) {
                        if(up) {
                            m->selected = m->selected == 0 ? n - 1 : m->selected - 1;
                        } else {
                            m->selected = m->selected + 1 >= n ? 0 : m->selected + 1;
                        }
                        radiogeddon_db_view_keep_visible(m);
                    }
                },
                true);
            consumed = true;
        }
    }
    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        out = RadioGeddonDbViewEventDetails;
        fire = true;
    } else if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
            out = RadioGeddonDbViewEventOpen;
            fire = true;
        } else if(event->key == InputKeyLeft) {
            out = RadioGeddonDbViewEventSort;
            fire = true;
        } else if(event->key == InputKeyRight) {
            out = RadioGeddonDbViewEventOptions;
            fire = true;
        }
    }
    if(fire) {
        if(instance->callback) instance->callback(out, instance->context);
        consumed = true;
    }
    return consumed;
}

RadioGeddonDbView* radiogeddon_db_view_alloc(void) {
    RadioGeddonDbView* instance = malloc(sizeof(RadioGeddonDbView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    instance->locked = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonDbModel));
    view_set_draw_callback(instance->view, radiogeddon_db_view_draw);
    view_set_input_callback(instance->view, radiogeddon_db_view_input);
    with_view_model(
        instance->view,
        RadioGeddonDbModel * m,
        {
            m->db = NULL;
            m->selected = 0;
            m->top = 0;
        },
        false);
    return instance;
}

void radiogeddon_db_view_free(RadioGeddonDbView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_db_view_get_view(RadioGeddonDbView* instance) {
    return instance->view;
}

void radiogeddon_db_view_set_callback(
    RadioGeddonDbView* instance,
    RadioGeddonDbViewCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_db_view_set_db(RadioGeddonDbView* instance, RadioGeddonDb* db) {
    with_view_model(
        instance->view,
        RadioGeddonDbModel * m,
        {
            m->db = db;
            m->selected = 0;
            m->top = 0;
        },
        true);
}

RadioGeddonDb* radiogeddon_db_view_lock(RadioGeddonDbView* instance) {
    instance->locked = view_get_model(instance->view);
    return instance->locked->db;
}

void radiogeddon_db_view_unlock(RadioGeddonDbView* instance, bool to_top) {
    RadioGeddonDbModel* m = instance->locked;
    instance->locked = NULL;
    if(m->db) radiogeddon_db_apply(m->db);
    size_t n = m->db ? m->db->view_count : 0;
    if(to_top || m->selected >= n) {
        m->selected = 0;
        m->top = 0;
    }
    radiogeddon_db_view_keep_visible(m);
    view_commit_model(instance->view, true);
}

size_t radiogeddon_db_view_get_selected(RadioGeddonDbView* instance) {
    size_t sel = 0;
    with_view_model(instance->view, RadioGeddonDbModel * m, { sel = m->selected; }, false);
    return sel;
}

void radiogeddon_db_view_set_selected(RadioGeddonDbView* instance, size_t position) {
    with_view_model(
        instance->view,
        RadioGeddonDbModel * m,
        {
            size_t n = m->db ? m->db->view_count : 0;
            m->selected = n == 0 ? 0 : (position < n ? position : n - 1);
            m->top = 0;
            radiogeddon_db_view_keep_visible(m);
        },
        true);
}
