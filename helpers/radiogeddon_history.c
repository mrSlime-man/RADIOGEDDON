#include "radiogeddon_history.h"

typedef struct {
    FuriString* name;
    FuriString* text;
    FuriString* serialized;
    uint8_t hash;
    bool has_serialized;
} RadioGeddonHistoryItem;

struct RadioGeddonHistory {
    RadioGeddonHistoryItem items[RADIOGEDDON_HISTORY_MAX];
    size_t count;
};

RadioGeddonHistory* radiogeddon_history_alloc(void) {
    RadioGeddonHistory* instance = malloc(sizeof(RadioGeddonHistory));
    memset(instance, 0, sizeof(RadioGeddonHistory));
    return instance;
}

static void radiogeddon_history_item_free(RadioGeddonHistoryItem* item) {
    if(item->name) furi_string_free(item->name);
    if(item->text) furi_string_free(item->text);
    if(item->serialized) furi_string_free(item->serialized);
    item->name = NULL;
    item->text = NULL;
    item->serialized = NULL;
    item->has_serialized = false;
}

void radiogeddon_history_reset(RadioGeddonHistory* instance) {
    for(size_t i = 0; i < instance->count; i++) {
        radiogeddon_history_item_free(&instance->items[i]);
    }
    instance->count = 0;
}

void radiogeddon_history_free(RadioGeddonHistory* instance) {
    radiogeddon_history_reset(instance);
    free(instance);
}

size_t radiogeddon_history_count(RadioGeddonHistory* instance) {
    return instance->count;
}

bool radiogeddon_history_add(
    RadioGeddonHistory* instance,
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized) {
    // De-duplicate against the most recent entry.
    if(instance->count > 0) {
        RadioGeddonHistoryItem* last = &instance->items[instance->count - 1];
        if(last->hash == hash && furi_string_equal_str(last->name, protocol_name)) {
            return false;
        }
    }

    if(instance->count >= RADIOGEDDON_HISTORY_MAX) {
        // Drop the oldest entry (shift down).
        radiogeddon_history_item_free(&instance->items[0]);
        for(size_t i = 1; i < instance->count; i++) {
            instance->items[i - 1] = instance->items[i];
        }
        instance->count--;
    }

    RadioGeddonHistoryItem* item = &instance->items[instance->count];
    item->name = furi_string_alloc_set(protocol_name);
    item->text = furi_string_alloc_set(text);
    item->hash = hash;
    if(serialized) {
        item->serialized = furi_string_alloc_set(serialized);
        item->has_serialized = true;
    } else {
        item->serialized = furi_string_alloc();
        item->has_serialized = false;
    }
    instance->count++;
    return true;
}

const char* radiogeddon_history_get_name(RadioGeddonHistory* instance, size_t index) {
    if(index >= instance->count) return "";
    return furi_string_get_cstr(instance->items[index].name);
}

FuriString* radiogeddon_history_get_text(RadioGeddonHistory* instance, size_t index) {
    if(index >= instance->count) return NULL;
    return instance->items[index].text;
}

FuriString* radiogeddon_history_get_serialized(RadioGeddonHistory* instance, size_t index) {
    if(index >= instance->count) return NULL;
    return instance->items[index].serialized;
}

bool radiogeddon_history_has_serialized(RadioGeddonHistory* instance, size_t index) {
    if(index >= instance->count) return false;
    return instance->items[index].has_serialized;
}
