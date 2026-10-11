#include "radiogeddon_scene.h"

#if RG_FEATURE_SESSIONS

// Full edition: grouping suggestions. The Database index is read, sorted by
// date, and recordings with the same frequency, protocol and frame length
// saved close together are offered as groups (rg_session_suggest). Choosing
// one asks first, then creates a session naming them; nothing is merged,
// moved or deleted. Index and suggestions are freed on exit.

typedef enum {
    SessionGroupsEventCreate = 70,
    SessionGroupsEventCancel,
    SessionGroupsEventEmpty,
    SessionGroupsGroup = 100, // + group index
} SessionGroupsEvent;

typedef struct {
    RadioGeddonDb* db;
    RgSessionItem* items;
    int8_t* group_of;
    size_t count;
    RgSessionGroup group[RG_SESSION_MAX_GROUPS];
    size_t groups;
    size_t chosen;
} RadioGeddonGroups;

static RadioGeddonGroups* radiogeddon_groups;
/* Set just before a message is shown over the list: keep it for the return. */
static bool radiogeddon_groups_keep;

static void radiogeddon_scene_session_groups_free(void) {
    RadioGeddonGroups* g = radiogeddon_groups;
    if(!g) return;
    radiogeddon_db_free(g->db);
    free(g->items);
    free(g->group_of);
    free(g);
    radiogeddon_groups = NULL;
}

static void radiogeddon_scene_session_groups_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void
    radiogeddon_scene_session_groups_label(const RgSessionGroup* g, char* out, size_t size) {
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(g->frequency, freq, sizeof(freq));
    if(g->bits) {
        snprintf(
            out, size, "%s %s %ub x%u", freq, g->protocol, (unsigned)g->bits, (unsigned)g->count);
    } else {
        snprintf(
            out, size, "%s %s x%u", freq, g->protocol[0] ? g->protocol : "?", (unsigned)g->count);
    }
}

static void radiogeddon_scene_session_groups_show(RadioGeddonApp* app) {
    RadioGeddonGroups* g = radiogeddon_groups;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Suggested groups");
    char label[48];
    for(size_t i = 0; i < g->groups; i++) {
        radiogeddon_scene_session_groups_label(&g->group[i], label, sizeof(label));
        submenu_add_item(
            submenu, label, SessionGroupsGroup + i, radiogeddon_scene_session_groups_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_session_groups_on_enter(void* context) {
    RadioGeddonApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionGroups, 0);
    if(radiogeddon_groups) {
        radiogeddon_scene_session_groups_show(app); // back from a message
        return;
    }
    radiogeddon_scene_show_progress(app, "Reading files...");
    RadioGeddonDbStatus status;
    RadioGeddonDb* db =
        radiogeddon_db_load(app->storage, &status, radiogeddon_scene_progress, app);
    radiogeddon_scene_progress_end(app);
    if(!db) {
        app->message_header = "Not enough memory";
        app->message_text = "Close other apps\nand try again.";
        view_dispatcher_send_custom_event(app->view_dispatcher, SessionGroupsEventEmpty);
        return;
    }
    RadioGeddonGroups* g = malloc(sizeof(RadioGeddonGroups));
    memset(g, 0, sizeof(*g));
    g->db = db;
    db->query.sort = RgDbSortDate;
    db->query.show = RgDbShowAll;
    db->query.search[0] = '\0';
    radiogeddon_db_apply(db);
    g->count = db->view_count;
    g->items = malloc((g->count ? g->count : 1) * sizeof(RgSessionItem));
    g->group_of = malloc(g->count ? g->count : 1);
    for(size_t i = 0; i < g->count; i++) {
        const RgDbEntry* e = radiogeddon_db_at(db, i);
        RgSessionItem* it = &g->items[i];
        it->file = rg_db_name(&db->db, e);
        it->frequency = (e->kind == RgDbKindRaw || e->kind == RgDbKindProtocol) ? e->frequency : 0;
        it->protocol = e->kind == RgDbKindRaw ? "RAW" : e->protocol;
        it->bits = e->kind == RgDbKindProtocol ? e->bits : 0;
        it->mtime = e->mtime;
    }
    g->groups = rg_session_suggest(g->items, g->count, g->group_of, g->group);
    radiogeddon_groups = g;
    if(g->groups == 0) {
        app->message_header = "No groups found";
        app->message_text =
            "No two recordings share\nfrequency, protocol and\nlength close in time.";
        view_dispatcher_send_custom_event(app->view_dispatcher, SessionGroupsEventEmpty);
        return;
    }
    radiogeddon_scene_session_groups_show(app);
}

/* A free session name from the group: "433.92 Princeton", "... 2" ... */
static bool
    radiogeddon_scene_session_groups_name(RadioGeddonApp* app, const RgSessionGroup* g, char* out) {
    char base[RG_SESSION_NAME_MAX];
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(g->frequency, freq, sizeof(freq));
    snprintf(base, sizeof(base), "%s %s", freq, g->protocol[0] ? g->protocol : "group");
    for(char* c = base; *c; c++)
        if(strchr("/\\:*?\"<>|", *c)) *c = '_';
    for(unsigned k = 1; k < 100; k++) {
        if(k == 1) {
            strlcpy(out, base, RG_SESSION_NAME_MAX);
        } else {
            snprintf(out, RG_SESSION_NAME_MAX, "%.27s %u", base, k);
        }
        if(rg_session_name_valid(out) && !radiogeddon_session_exists(app->storage, out))
            return true;
    }
    return false;
}

static void radiogeddon_scene_session_groups_create(RadioGeddonApp* app) {
    RadioGeddonGroups* g = radiogeddon_groups;
    const RgSessionGroup* grp = &g->group[g->chosen];
    RgSession* s = malloc(sizeof(RgSession));
    char name[RG_SESSION_NAME_MAX], created[20];
    radiogeddon_session_now(created, sizeof(created));
    bool ok = radiogeddon_scene_session_groups_name(app, grp, name);
    if(ok) {
        rg_session_init(s, name, created);
        for(size_t i = 0; i < g->count; i++)
            if(g->group_of[i] == (int8_t)g->chosen) rg_session_add(s, g->items[i].file);
        ok = radiogeddon_session_save(app->storage, s);
    }
    free(s);
    if(ok) {
        notification_message(app->notifications, &sequence_success);
        snprintf(app->tx_text, sizeof(app->tx_text), "%s", name);
        radiogeddon_groups_keep = true;
        radiogeddon_scene_show_message(app, "Session created", app->tx_text);
    } else {
        radiogeddon_groups_keep = true;
        radiogeddon_scene_show_message(app, "Not created", "Check the SD card.");
    }
}

bool radiogeddon_scene_session_groups_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    RadioGeddonGroups* g = radiogeddon_groups;
    // Back while asking returns to the list.
    if(event.type == SceneManagerEventTypeBack && g &&
       scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessionGroups)) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionGroups, 0);
        radiogeddon_scene_session_groups_show(app);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SessionGroupsEventEmpty) {
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    if(!g) return false;
    if(event.event >= SessionGroupsGroup && event.event < SessionGroupsGroup + g->groups) {
        g->chosen = event.event - SessionGroupsGroup;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionGroups, 1);
        char label[48];
        radiogeddon_scene_session_groups_label(&g->group[g->chosen], label, sizeof(label));
        snprintf(
            app->tx_text,
            sizeof(app->tx_text),
            "%s\nA suggestion only: the\nfiles are not changed.",
            label);
        radiogeddon_scene_show_confirm(
            app,
            "Make a session?",
            app->tx_text,
            "Create",
            SessionGroupsEventCreate,
            SessionGroupsEventCancel);
        return true;
    }
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionGroups, 0);
    if(event.event == SessionGroupsEventCreate) {
        radiogeddon_scene_session_groups_create(app);
        return true;
    }
    if(event.event == SessionGroupsEventCancel) {
        radiogeddon_scene_session_groups_show(app);
        return true;
    }
    return false;
}

void radiogeddon_scene_session_groups_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
    if(!radiogeddon_groups_keep) radiogeddon_scene_session_groups_free();
    radiogeddon_groups_keep = false;
}

#endif
