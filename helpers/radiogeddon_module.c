#include "radiogeddon_modules.h"

#if RG_EDITION_FULL

#include <flipper_application/flipper_application.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include "rg_elf.h"

#define TAG "RadioGeddonModule"

/* Kept free beyond the module and its work (GUI, storage, other threads). */
#define RADIOGEDDON_MODULE_MARGIN (4u * 1024u)

/* The loader's own work beyond the module's sections (symbol and relocation
 * caches, the manager and descriptor), generously. */
#define RADIOGEDDON_MODULE_LOADER (6u * 1024u)

struct RadioGeddonModule {
    PluginManager* manager;
};

static bool radiogeddon_module_read(void* context, uint32_t offset, void* buf, size_t len) {
    File* file = context;
    return storage_file_seek(file, offset, true) && storage_file_read(file, buf, len) == len;
}

/* Bytes the module's code and data take once loaded (rg_elf.h); 0 if its
 * file cannot be read as an ELF32 object. */
static size_t radiogeddon_module_sections(Storage* storage, const char* path) {
    File* file = storage_file_alloc(storage);
    size_t total = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        total = rg_elf_alloc_bytes(radiogeddon_module_read, file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return total;
}

RadioGeddonModule* radiogeddon_module_load(
    Storage* storage,
    const char* file,
    size_t extra_heap,
    const void** ep,
    RadioGeddonModuleStatus* status) {
    *ep = NULL;
    char path[96];
    snprintf(path, sizeof(path), "%s", APP_ASSETS_PATH("plugins/"));
    strlcat(path, file, sizeof(path));
    FileInfo info;
    if(storage_common_stat(storage, path, &info) != FSE_OK) {
        FURI_LOG_E(TAG, "%s missing", path);
        *status = RadioGeddonModuleMissing;
        return NULL;
    }
    // What the loader allocates: the module's sections plus its own work.
    // An unreadable header falls back to the whole file's size, which bounds
    // it from above.
    size_t sections = radiogeddon_module_sections(storage, path);
    size_t load = sections ? sections + RADIOGEDDON_MODULE_LOADER : (size_t)info.size;
    size_t need = load + extra_heap + RADIOGEDDON_MODULE_MARGIN;
    if(memmgr_heap_get_max_free_block() < need) {
        FURI_LOG_W(
            TAG,
            "%s needs %u, %u free",
            file,
            (unsigned)need,
            (unsigned)memmgr_heap_get_max_free_block());
        *status = RadioGeddonModuleNoMemory;
        return NULL;
    }
    RadioGeddonModule* module = malloc(sizeof(RadioGeddonModule));
    module->manager = plugin_manager_alloc(
        RADIOGEDDON_MODULE_APPID, RADIOGEDDON_MODULE_API, firmware_api_interface);
    PluginManagerError err = plugin_manager_load_single(module->manager, path);
    if(err != PluginManagerErrorNone || plugin_manager_get_count(module->manager) != 1) {
        FURI_LOG_E(TAG, "%s not loaded: %d", file, (int)err);
        plugin_manager_free(module->manager);
        free(module);
        *status = RadioGeddonModuleLoadFailed;
        return NULL;
    }
    *ep = plugin_manager_get_ep(module->manager, 0);
    *status = RadioGeddonModuleOk;
    return module;
}

void radiogeddon_module_unload(RadioGeddonModule* module) {
    if(!module) return;
    plugin_manager_free(module->manager);
    free(module);
}

void radiogeddon_module_error(
    RadioGeddonModuleStatus status,
    const char** header,
    const char** text) {
    switch(status) {
    case RadioGeddonModuleNoMemory:
        *header = "Not enough memory";
        *text = "This tool does not fit\nnow. Restart the\nFlipper and retry.";
        break;
    case RadioGeddonModuleMissing:
        *header = "Tool missing";
        *text = "Reinstall the app:\nits plugins folder is\nincomplete.";
        break;
    default:
        *header = "Tool not loaded";
        *text = "It does not match this\nversion or firmware.\nReinstall the app.";
        break;
    }
}

#endif
