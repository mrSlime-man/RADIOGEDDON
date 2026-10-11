/**
 * @file radiogeddon_modules.h
 * @brief Full edition: optional tools loaded only while they are used.
 *
 * A .fap is loaded whole into RAM and stays there while the app runs. The
 * Full edition's optional tools (the Bitstream Explorer's screen, the
 * Multi-Capture Compare engine, the Waterfall's screen) are built as
 * plugins instead (.fal, packed inside the .fap and unpacked to its assets
 * folder by the firmware), loaded when their screen opens and unloaded when
 * it closes, so their code takes heap only while in use.
 *
 * A module uses the firmware's API only: everything from the app it needs
 * is passed in (pointers to data, callbacks). Its entry point returns one of
 * the structs below. RADIOGEDDON_MODULE_API changes whenever any of them
 * does, so a stale .fal from an older version is refused, never called.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_EDITION_FULL

#include <furi.h>
#include <gui/view.h>
#include <storage/storage.h>
#include "radiogeddon_progress.h"

/* The plugin manager's application id and API version for every module. */
#define RADIOGEDDON_MODULE_APPID "radiogeddon_full"
#define RADIOGEDDON_MODULE_API   1u

/* File names in the app's assets folder (APP_ASSETS_PATH("plugins/...")). */
#define RADIOGEDDON_MODULE_BITS      "radiogeddon_bits.fal"
#define RADIOGEDDON_MODULE_MULTI     "radiogeddon_multi.fal"
#define RADIOGEDDON_MODULE_WATERFALL "radiogeddon_wf.fal"
#define RADIOGEDDON_MODULE_UNKNOWN   "radiogeddon_unknown.fal"
#define RADIOGEDDON_MODULE_SESSIONS  "radiogeddon_sessions.fal"

#include "radiogeddon_analysis.h"
/* Unknown Protocol Analysis report (radiogeddon_analysis_unknown). */
typedef struct {
    RadioGeddonUnknownFn unknown;
} RadioGeddonUnknownModule;

#if RG_FEATURE_BITSTREAM
#include "../views/radiogeddon_bits_view.h"
typedef struct {
    RadioGeddonBitsView* (*alloc)(void);
    void (*free)(RadioGeddonBitsView* view);
    View* (*get_view)(RadioGeddonBitsView* view);
    void (*set_analysis)(RadioGeddonBitsView* view, const RgAnalysis* analysis);
} RadioGeddonBitsModule;
#endif

#if RG_FEATURE_MULTI_COMPARE
/** Told when the comparison starts on file @p index of @p count. */
typedef void (*RadioGeddonMultiFileCallback)(void* context, size_t index, size_t count);

typedef struct {
    /** Heap one comparison of @p count files takes (summaries, one analysis). */
    size_t (*memory)(size_t count);
    /**
     * Analyse @p paths one after another and write the report (labelled
     * lines) into @p report (@p size bytes). @p heading, if any, starts it.
     * Returns the report's length.
     */
    size_t (*compare)(
        Storage* storage,
        const char* const* paths,
        size_t count,
        const char* heading,
        char* report,
        size_t size,
        RadioGeddonMultiFileCallback on_file,
        RadioGeddonProgressCallback progress,
        void* context);
} RadioGeddonMultiModule;
#endif

#if RG_FEATURE_WATERFALL
#include "../views/radiogeddon_waterfall_view.h"
typedef struct {
    RadioGeddonWaterfallView* (*alloc)(void);
    void (*free)(RadioGeddonWaterfallView* view);
    View* (*get_view)(RadioGeddonWaterfallView* view);
    void (
        *set_callback)(RadioGeddonWaterfallView* view, RadioGeddonWaterfallCallback cb, void* ctx);
    void (*update)(RadioGeddonWaterfallView* view, RadioGeddonWaterfallFrameFn frame, void* engine);
    void (*set_style)(RadioGeddonWaterfallView* view, uint8_t span_index, bool noise_comp);
    void (*get_style)(RadioGeddonWaterfallView* view, uint8_t* span_index, bool* noise_comp);
    void (*set_external)(RadioGeddonWaterfallView* view, bool external);
    uint32_t (*get_cursor_hz)(RadioGeddonWaterfallView* view);
    uint16_t (*get_cursor)(RadioGeddonWaterfallView* view);
    void (*set_cursor)(RadioGeddonWaterfallView* view, uint16_t column);
} RadioGeddonWaterfallModule;
#endif

#if RG_FEATURE_SESSIONS
#include <gui/scene_manager.h>
#include "radiogeddon_host.h"
/* The Sessions screens, in the order of RadioGeddonSessionsScene. */
typedef enum {
    RadioGeddonSessionsSceneList,
    RadioGeddonSessionsSceneName,
    RadioGeddonSessionsSceneMenu,
    RadioGeddonSessionsSceneSignals,
    RadioGeddonSessionsSceneGroups,
    RadioGeddonSessionsSceneCount,
} RadioGeddonSessionsScene;

typedef struct {
    void (*set_host)(const RadioGeddonHost* host);
    void (*on_enter[RadioGeddonSessionsSceneCount])(void* context);
    bool (*on_event[RadioGeddonSessionsSceneCount])(void* context, SceneManagerEvent event);
    void (*on_exit[RadioGeddonSessionsSceneCount])(void* context);
} RadioGeddonSessionsModule;
#endif

/* ---- host side --------------------------------------------------------------- */

typedef enum {
    RadioGeddonModuleOk,
    RadioGeddonModuleMissing, // the .fal is not in the assets folder
    RadioGeddonModuleNoMemory, // it would not fit in the free heap
    RadioGeddonModuleLoadFailed, // refused by the loader (version, symbols)
} RadioGeddonModuleStatus;

typedef struct RadioGeddonModule RadioGeddonModule;

/**
 * Load module @p file if it fits with @p extra_heap bytes to spare (the
 * memory its work will need); returns it with its entry point in @p ep, or
 * NULL with @p status saying why. Never allocates past the free heap: the
 * check comes first, from the module's file size.
 */
RadioGeddonModule* radiogeddon_module_load(
    Storage* storage,
    const char* file,
    size_t extra_heap,
    const void** ep,
    RadioGeddonModuleStatus* status);

/** Unload; nothing it allocated may still be in use (views removed first). */
void radiogeddon_module_unload(RadioGeddonModule* module);

/** Header and text for a refusal (static strings). */
void radiogeddon_module_error(
    RadioGeddonModuleStatus status,
    const char** header,
    const char** text);

#endif
