#include "radiogeddon_scene.h"

// A short error or notice on its own screen (radiogeddon_scene_show_message).

#define MESSAGE_SHOW_MS 3000u

typedef enum {
    MessageEventDone = 800,
} MessageEvent;

static void radiogeddon_scene_message_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, MessageEventDone);
}

void radiogeddon_scene_message_on_enter(void* context) {
    RadioGeddonApp* app = context;
    notification_message(app->notifications, &sequence_error);
    popup_reset(app->popup);
    popup_set_header(
        app->popup,
        app->message_header ? app->message_header : "",
        64,
        14,
        AlignCenter,
        AlignCenter);
    popup_set_text(
        app->popup, app->message_text ? app->message_text : "", 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_message_cb);
    popup_set_timeout(app->popup, MESSAGE_SHOW_MS);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

bool radiogeddon_scene_message_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == MessageEventDone) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void radiogeddon_scene_message_on_exit(void* context) {
    RadioGeddonApp* app = context;
    popup_reset(app->popup);
    app->message_header = NULL;
    app->message_text = NULL;
}
