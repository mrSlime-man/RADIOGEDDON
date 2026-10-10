#include "radiogeddon_scene.h"

// Database options: sort order, which files to show, name search and reload.
// Scene state 1 while the search keyboard is shown.

#define DB_OPTIONS_PROTOCOLS 16u

typedef enum {
    DbOptionsRowSort,
    DbOptionsRowShow,
    DbOptionsRowSearch,
    DbOptionsRowReload,
    DbOptionsRowFiles,
} DbOptionsRow;

typedef enum {
    DbOptionsEventSearch = 500,
    DbOptionsEventSearchDone,
    DbOptionsEventReload,
} DbOptionsEvent;

typedef enum {
    DbOptionsStateList = 0,
    DbOptionsStateSearch = 1,
} DbOptionsState;

// "Show" values: these filters, then one value per protocol in the index.
static const RgDbShow db_options_filters[] = {
    RgDbShowAll,
    RgDbShowRaw,
    RgDbShowDecoded,
    RgDbShowDuplicates,
    RgDbShowDamaged,
};
#define DB_OPTIONS_FILTERS COUNT_OF(db_options_filters)

static size_t radiogeddon_scene_db_options_protocols(RadioGeddonApp* app, const char** out) {
    return rg_db_protocols(&app->db->db, out, DB_OPTIONS_PROTOCOLS);
}

static void radiogeddon_scene_db_options_sort_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    RgDbSort sort = (RgDbSort)variable_item_get_current_value_index(item);
    RadioGeddonDb* db = radiogeddon_db_view_lock(app->db_view);
    db->query.sort = sort;
    radiogeddon_db_view_unlock(app->db_view, true);
    variable_item_set_current_value_text(item, rg_db_sort_name(sort));
}

static void radiogeddon_scene_db_options_show_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    const char* protocols[DB_OPTIONS_PROTOCOLS];
    size_t count = radiogeddon_scene_db_options_protocols(app, protocols);

    RadioGeddonDb* db = radiogeddon_db_view_lock(app->db_view);
    if(index < DB_OPTIONS_FILTERS) {
        db->query.show = db_options_filters[index];
        db->query.protocol[0] = '\0';
        variable_item_set_current_value_text(item, rg_db_show_name(db->query.show));
    } else if(index - DB_OPTIONS_FILTERS < count) {
        db->query.show = RgDbShowProtocol;
        strlcpy(
            db->query.protocol, protocols[index - DB_OPTIONS_FILTERS], sizeof(db->query.protocol));
        variable_item_set_current_value_text(item, db->query.protocol);
    }
    radiogeddon_db_view_unlock(app->db_view, true);
}

static void radiogeddon_scene_db_options_enter(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    if(index == DbOptionsRowSearch) {
        view_dispatcher_send_custom_event(app->view_dispatcher, DbOptionsEventSearch);
    } else if(index == DbOptionsRowReload) {
        view_dispatcher_send_custom_event(app->view_dispatcher, DbOptionsEventReload);
    }
}

static void radiogeddon_scene_db_options_show_list(RadioGeddonApp* app, uint8_t selected) {
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    const char* protocols[DB_OPTIONS_PROTOCOLS];
    size_t count = radiogeddon_scene_db_options_protocols(app, protocols);
    const RadioGeddonDb* db = app->db;
    const RgDbQuery* q = &db->query;

    VariableItem* item = variable_item_list_add(
        list, "Sort by", RgDbSortCount, radiogeddon_scene_db_options_sort_changed, app);
    variable_item_set_current_value_index(item, q->sort);
    variable_item_set_current_value_text(item, rg_db_sort_name(q->sort));

    item = variable_item_list_add(
        list, "Show", DB_OPTIONS_FILTERS + count, radiogeddon_scene_db_options_show_changed, app);
    uint8_t show = 0;
    const char* show_text = NULL;
    if(q->show == RgDbShowProtocol) {
        for(size_t i = 0; i < count; i++) {
            if(strcmp(protocols[i], q->protocol) == 0) {
                show = DB_OPTIONS_FILTERS + i;
                show_text = protocols[i];
            }
        }
    } else {
        for(size_t i = 0; i < DB_OPTIONS_FILTERS; i++) {
            if(db_options_filters[i] == q->show) {
                show = i;
                show_text = rg_db_show_name(q->show);
            }
        }
    }
    if(!show_text) {
        // The filtered protocol is gone after a reload: show everything.
        RadioGeddonDb* locked = radiogeddon_db_view_lock(app->db_view);
        locked->query.show = RgDbShowAll;
        locked->query.protocol[0] = '\0';
        radiogeddon_db_view_unlock(app->db_view, true);
        show_text = rg_db_show_name(RgDbShowAll);
    }
    variable_item_set_current_value_index(item, show);
    variable_item_set_current_value_text(item, show_text);

    item = variable_item_list_add(list, "Search name", 1, NULL, app);
    variable_item_set_current_value_text(item, q->search[0] ? q->search : "Off");

    variable_item_list_add(list, "Reload from SD", 1, NULL, app);

    item = variable_item_list_add(list, "Files indexed", 1, NULL, app);
    char files[24];
    if(db->truncated) {
        snprintf(
            files, sizeof(files), "%u of %u", (unsigned)db->db.count, (unsigned)db->total_files);
    } else {
        snprintf(files, sizeof(files), "%u", (unsigned)db->db.count);
    }
    variable_item_set_current_value_text(item, files);

    variable_item_list_set_enter_callback(list, radiogeddon_scene_db_options_enter, app);
    variable_item_list_set_selected_item(list, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

static void radiogeddon_scene_db_options_search_done(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, DbOptionsEventSearchDone);
}

static void radiogeddon_scene_db_options_show_search(RadioGeddonApp* app) {
    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneDbOptions, DbOptionsStateSearch);
    strlcpy(app->text_store, app->db->query.search, sizeof(app->text_store));
    TextInput* text_input = app->text_input;
    text_input_reset(text_input);
    text_input_set_header_text(text_input, "Name contains (empty: all)");
    text_input_set_result_callback(
        text_input,
        radiogeddon_scene_db_options_search_done,
        app,
        app->text_store,
        RG_DB_SEARCH_MAX,
        true);
    text_input_set_minimum_length(text_input, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

static void radiogeddon_scene_db_options_back_to_list(RadioGeddonApp* app) {
    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneDbOptions, DbOptionsStateList);
    radiogeddon_scene_db_options_show_list(app, DbOptionsRowSearch);
}

void radiogeddon_scene_db_options_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // Opened from the Database list, which keeps the index for this screen.
    furi_check(app->db && app->db_view);
    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneDbOptions, DbOptionsStateList);
    radiogeddon_scene_db_options_show_list(app, DbOptionsRowSort);
}

bool radiogeddon_scene_db_options_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DbOptionsEventSearch:
            radiogeddon_scene_db_options_show_search(app);
            consumed = true;
            break;
        case DbOptionsEventSearchDone: {
            RadioGeddonDb* db = radiogeddon_db_view_lock(app->db_view);
            strlcpy(db->query.search, app->text_store, sizeof(db->query.search));
            radiogeddon_db_view_unlock(app->db_view, true);
            radiogeddon_scene_db_options_back_to_list(app);
            consumed = true;
            break;
        }
        case DbOptionsEventReload:
            app->db_dirty = true;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Back from the keyboard returns to the options, search unchanged.
        if(scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneDbOptions) ==
           DbOptionsStateSearch) {
            radiogeddon_scene_db_options_back_to_list(app);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_db_options_on_exit(void* context) {
    RadioGeddonApp* app = context;
    variable_item_list_set_enter_callback(app->var_item_list, NULL, NULL);
    variable_item_list_reset(app->var_item_list);
    text_input_reset(app->text_input);
}
