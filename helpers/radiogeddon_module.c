#include "radiogeddon_modules.h"

#if RG_EDITION_FULL

#include <flipper_application/flipper_application.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>
#include "rg_elf.h"
#include "radiogeddon_memdiag.h"

#define TAG "RadioGeddonModule"

/* Kept free beyond the module and its work (GUI, storage, other threads). */
#define RADIOGEDDON_MODULE_MARGIN (4u * 1024u)

/* The loader's fixed structures besides what the module's file itself
 * makes it allocate (rg_elf.h): the ELF file and application records, the
 * manifest, the plugin manager and its list, the open file and the storage
 * service's file object with its sector buffer, name strings; about 1.2 KB
 * in firmware 1.4.3, generously. Beta 10 reserved a flat 6 KB for all of the
 * loader's work, which is less than the Range Scanner module's relocation
 * tables and file object alone and far more than About's. */
#define RADIOGEDDON_MODULE_LOADER (2u * 1024u)

struct RadioGeddonModule {
    PluginManager* manager;
};

static bool radiogeddon_module_read(void* context, uint32_t offset, void* buf, size_t len) {
    File* file = context;
    return storage_file_seek(file, offset, true) && storage_file_read(file, buf, len) == len;
}

/* What loading the module takes from the heap (rg_elf.h); false if its file
 * cannot be read as an ELF32 object. */
static bool radiogeddon_module_cost(Storage* storage, const char* path, RgElfCost* cost) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              rg_elf_load_cost(radiogeddon_module_read, file, cost);
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

size_t radiogeddon_module_need(const RgElfCost* cost, size_t extra_heap) {
    // The relocation tables are freed before the module's code runs, so its
    // work never coexists with them: the larger of the two. They are the
    // last blocks the loader takes (after every section), so freeing them
    // gives their space back to the free block the work then uses.
    size_t later = cost->temporary > extra_heap ? cost->temporary : extra_heap;
    return cost->resident + RADIOGEDDON_MODULE_LOADER + later + RADIOGEDDON_MODULE_MARGIN;
}

static RadioGeddonModule* radiogeddon_module_load_timed(
    Storage* storage,
    const char* file,
    size_t extra_heap,
    const void** ep,
    RadioGeddonModuleStatus* status);

RadioGeddonModule* radiogeddon_module_load(
    Storage* storage,
    const char* file,
    size_t extra_heap,
    const void** ep,
    RadioGeddonModuleStatus* status) {
    uint32_t start = furi_get_tick();
    RadioGeddonModule* module =
        radiogeddon_module_load_timed(storage, file, extra_heap, ep, status);
    radiogeddon_memdiag_module(file, (int)*status, furi_get_tick() - start);
    return module;
}

static RadioGeddonModule* radiogeddon_module_load_timed(
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
    // What the loader allocates (rg_elf.h) and then the module's work. An
    // unreadable header falls back to the whole file's size for the loader,
    // which bounds what it loads from above, added to the work.
    RgElfCost cost;
    size_t need;
    if(radiogeddon_module_cost(storage, path, &cost)) {
        need = radiogeddon_module_need(&cost, extra_heap);
    } else {
        need = (size_t)info.size + RADIOGEDDON_MODULE_LOADER + extra_heap +
               RADIOGEDDON_MODULE_MARGIN;
    }
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
