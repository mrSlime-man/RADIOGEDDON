/**
 * @file radiogeddon_db_view.h
 * @brief Signal Database list: name, type and, for the highlighted file, its
 *        frequency, date and duplicate count.
 *
 * Shows a RadioGeddonDb owned by the caller. The query (sort, filter, search)
 * is changed between radiogeddon_db_view_lock() and _unlock(), so the list is
 * never redrawn while it changes.
 *
 * Controls: Up/Down move (hold to repeat; wraps), Left changes the sort
 * order, Right opens the options, OK opens the highlighted file.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/radiogeddon_db.h"

typedef struct RadioGeddonDbView RadioGeddonDbView;

typedef enum {
    RadioGeddonDbViewEventOpen,
    RadioGeddonDbViewEventSort,
    RadioGeddonDbViewEventOptions,
} RadioGeddonDbViewEvent;

typedef void (*RadioGeddonDbViewCallback)(RadioGeddonDbViewEvent event, void* context);

RadioGeddonDbView* radiogeddon_db_view_alloc(void);
void radiogeddon_db_view_free(RadioGeddonDbView* instance);
View* radiogeddon_db_view_get_view(RadioGeddonDbView* instance);

void radiogeddon_db_view_set_callback(
    RadioGeddonDbView* instance,
    RadioGeddonDbViewCallback callback,
    void* context);

/** Show @p db (NULL shows "Loading..."). The selection starts at the top. */
void radiogeddon_db_view_set_db(RadioGeddonDbView* instance, RadioGeddonDb* db);

/** Lock the list to change db->query; unlock re-runs the query and redraws. */
RadioGeddonDb* radiogeddon_db_view_lock(RadioGeddonDbView* instance);
void radiogeddon_db_view_unlock(RadioGeddonDbView* instance, bool to_top);

/** Highlighted position in the current view (set_selected clamps to the end). */
size_t radiogeddon_db_view_get_selected(RadioGeddonDbView* instance);
void radiogeddon_db_view_set_selected(RadioGeddonDbView* instance, size_t position);
