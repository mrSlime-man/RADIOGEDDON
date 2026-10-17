#include "radiogeddon_scene.h"

#if RG_FEATURE_DOLPHIN_LAB

/*
 * Full edition: Dolphin Customization. Every change applies at once (the
 * dashboard and the room show it) and is saved on leaving. Accessories are
 * small sprites composed onto the dolphin when it is drawn; decorations
 * hang on the lab wall from their Research level on (a locked one is kept
 * but not shown). Classic draws none of this.
 */

typedef enum {
    CustomizeRowPhones,
    CustomizeRowGoggles,
    CustomizeRowAntenna,
    CustomizeRowTool,
    CustomizeRowExpression,
    CustomizeRowAccessory,
    CustomizeRowDecoration,
    CustomizeRowAnimation,
} CustomizeRow;

static const char* const customize_phones[] = {"Off", "On"};
static const char* const customize_goggles[RgLookGogglesCount] = {"As posed", "Always", "Off"};
static const char* const customize_antenna[RgLookAntennaCount] =
    {"Classic", "Whip", "Dish", "None"};
static const char* const customize_tool[RgLookToolCount] = {"None", "Wrench", "Probe", "Notebook"};
static const char* const customize_expression[RgLookExpressionCount] = {
    "Normal",
    "Cheerful",
    "Focused"};
static const char* const customize_accessory[RgLookAccessoryCount] =
    {"None", "Bow tie", "Scarf", "ID badge"};

static uint8_t customize_level(RadioGeddonApp* app) {
#if RG_FEATURE_JOURNAL
    return rg_journal_unlock_level(&app->research.journal, app->settings.research_xp);
#else
    UNUSED(app);
    return 1;
#endif
}

static void customize_decoration_text(RadioGeddonApp* app, VariableItem* item, uint8_t d) {
    char text[24];
    if(rg_decoration_unlocked((RgDecoration)d, customize_level(app))) {
        strlcpy(text, rg_decoration_name((RgDecoration)d), sizeof(text));
    } else {
        snprintf(
            text,
            sizeof(text),
            "%s (Lv %u)",
            rg_decoration_name((RgDecoration)d),
            (unsigned)rg_decoration_level((RgDecoration)d));
    }
    variable_item_set_current_value_text(item, text);
}

static void customize_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t v = variable_item_get_current_value_index(item);
    uint8_t row = variable_item_list_get_selected_item_index(app->var_item_list);
    RadioGeddonSettings* s = &app->settings;
    switch(row) {
    case CustomizeRowPhones:
        s->look[0] = v;
        variable_item_set_current_value_text(item, customize_phones[v]);
        break;
    case CustomizeRowGoggles:
        s->look[1] = v;
        variable_item_set_current_value_text(item, customize_goggles[v]);
        break;
    case CustomizeRowAntenna:
        s->look[2] = v;
        variable_item_set_current_value_text(item, customize_antenna[v]);
        break;
    case CustomizeRowTool:
        s->look[3] = v;
        variable_item_set_current_value_text(item, customize_tool[v]);
        break;
    case CustomizeRowExpression:
        s->look[4] = v;
        variable_item_set_current_value_text(item, customize_expression[v]);
        break;
    case CustomizeRowAccessory:
        s->look[5] = v;
        variable_item_set_current_value_text(item, customize_accessory[v]);
        break;
    case CustomizeRowDecoration:
        s->decoration = v;
        customize_decoration_text(app, item, v);
        break;
    case CustomizeRowAnimation:
        s->ui_animation = v;
        variable_item_set_current_value_text(item, rg_anim_level_name((RgAnimLevel)v));
        break;
    default:
        break;
    }
    // At once, without a restart.
    radiogeddon_ui_apply_style(app);
}

static VariableItem* customize_add(
    RadioGeddonApp* app,
    const char* label,
    uint8_t count,
    uint8_t value,
    const char* text) {
    VariableItem* item =
        variable_item_list_add(app->var_item_list, label, count, customize_changed, app);
    if(value >= count) value = 0;
    variable_item_set_current_value_index(item, value);
    if(text) variable_item_set_current_value_text(item, text);
    return item;
}

void radiogeddon_scene_customize_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // No enter action here: never keep another screen's callback.
    radiogeddon_scene_var_list_release(app);
    RgMascotLook look;
    memcpy(&look, app->settings.look, sizeof(look));
    rg_mascot_look_sanitize(&look);
    customize_add(app, "Headphones", 2, look.phones, customize_phones[look.phones]);
    customize_add(
        app, "Goggles", RgLookGogglesCount, look.goggles, customize_goggles[look.goggles]);
    customize_add(
        app, "Antenna", RgLookAntennaCount, look.antenna, customize_antenna[look.antenna]);
    customize_add(app, "Tool", RgLookToolCount, look.tool, customize_tool[look.tool]);
    customize_add(
        app,
        "Expression",
        RgLookExpressionCount,
        look.expression,
        customize_expression[look.expression]);
    customize_add(
        app,
        "Accessory",
        RgLookAccessoryCount,
        look.accessory,
        customize_accessory[look.accessory]);
    uint8_t deco = app->settings.decoration < RgDecoCount ? app->settings.decoration : 0;
    VariableItem* item = customize_add(app, "Wall", RgDecoCount, deco, NULL);
    customize_decoration_text(app, item, deco);
    uint8_t anim = app->settings.ui_animation < RgAnimCount ? app->settings.ui_animation :
                                                              RgAnimNormal;
    customize_add(app, "Animation", RgAnimCount, anim, rg_anim_level_name((RgAnimLevel)anim));
    variable_item_list_set_selected_item(app->var_item_list, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_customize_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_customize_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_app_save_settings(app);
    radiogeddon_scene_var_list_release(app);
}

#endif
