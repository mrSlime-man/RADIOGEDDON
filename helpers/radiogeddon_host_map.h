/**
 * @file radiogeddon_host_map.h
 * @brief Module builds: the app's helpers by name, through the host table.
 *
 * Included last by scenes/radiogeddon_scene.h, after the helpers are
 * declared; does nothing outside a module build. See radiogeddon_host.h.
 */
#pragma once

#include "radiogeddon_host.h"

#ifdef RADIOGEDDON_MODULE
/* Set by the module's set_host() before any scene of it runs. */
extern const RadioGeddonHost* radiogeddon_host;

#define radiogeddon_scene_show_message  (radiogeddon_host->show_message)
#define radiogeddon_scene_show_confirm  (radiogeddon_host->show_confirm)
#define radiogeddon_scene_show_busy     (radiogeddon_host->show_busy)
#define radiogeddon_scene_show_progress (radiogeddon_host->show_progress)
#define radiogeddon_scene_progress      (radiogeddon_host->progress)
#define radiogeddon_scene_progress_end  (radiogeddon_host->progress_end)
#define radiogeddon_scene_module_load   (radiogeddon_host->module_load)
#define radiogeddon_scene_module_unload (radiogeddon_host->module_unload)
#define radiogeddon_multi_add           (radiogeddon_host->multi_add)
#define radiogeddon_multi_clear         (radiogeddon_host->multi_clear)
#define radiogeddon_scene_multi_memory  (radiogeddon_host->multi_memory)
#define radiogeddon_research_record(app, event, count, note) \
    (radiogeddon_host->research_record((app), (uint32_t)(event), (count), (note)))
#define radiogeddon_research_step(app, path, step, arg) \
    (radiogeddon_host->research_step((app), (path), (uint32_t)(step), (arg)))
#define radiogeddon_research_set_context   (radiogeddon_host->research_set_context)
#define radiogeddon_research_context_path  (radiogeddon_host->research_context_path)
#define radiogeddon_research_set_similar   (radiogeddon_host->research_set_similar)
#define radiogeddon_research_session_added (radiogeddon_host->research_session_added)
#define radiogeddon_research_clear         (radiogeddon_host->research_clear)
#define radiogeddon_research_dismiss_tip   (radiogeddon_host->research_dismiss_tip)
#define radiogeddon_ui_event(app, event, arg) \
    (radiogeddon_host->ui_event((app), (uint32_t)(event), (arg)))
#define radiogeddon_ui_cancel                (radiogeddon_host->ui_cancel)
#define radiogeddon_ui_set_scanning          (radiogeddon_host->ui_set_scanning)
#define radiogeddon_ui_apply_style           (radiogeddon_host->ui_apply_style)
#define radiogeddon_app_save_settings        (radiogeddon_host->save_settings)
#define radiogeddon_scene_build_list         (radiogeddon_host->build_list)
#define radiogeddon_memdiag_sample           (radiogeddon_host->memdiag_sample)
#define radiogeddon_scanner_alloc            (radiogeddon_host->scanner_alloc)
#define radiogeddon_scanner_configure        (radiogeddon_host->scanner_configure)
#define radiogeddon_scanner_set_callback     (radiogeddon_host->scanner_set_callback)
#define radiogeddon_scanner_set_mode         (radiogeddon_host->scanner_set_mode)
#define radiogeddon_scanner_set_observer     (radiogeddon_host->scanner_set_observer)
#define radiogeddon_scanner_start            (radiogeddon_host->scanner_start)
#define radiogeddon_scanner_stop             (radiogeddon_host->scanner_stop)
#define radiogeddon_scanner_lock             (radiogeddon_host->scanner_lock)
#define radiogeddon_scanner_unlock           (radiogeddon_host->scanner_unlock)
#define radiogeddon_scanner_reset_learning   (radiogeddon_host->scanner_reset_learning)
#define radiogeddon_scanner_reset_peaks      (radiogeddon_host->scanner_reset_peaks)
#define radiogeddon_subghz_is_device_present (radiogeddon_host->subghz_is_device_present)
#define radiogeddon_subghz_set_preset        (radiogeddon_host->subghz_set_preset)
#define radiogeddon_session_capture_saved    (radiogeddon_host->session_capture_saved)
#define radiogeddon_session_active           (radiogeddon_host->session_active)
#define RADIOGEDDON_PRESET_LABEL(index)      (radiogeddon_host->preset_label(index))
#define radiogeddon_memdiag_stats            (radiogeddon_host->memdiag_stats)
#define radiogeddon_memdiag_diags            (radiogeddon_host->memdiag_diags)
#define radiogeddon_subghz_get_radio(subghz) \
    (radiogeddon_host->subghz_is_external(subghz) ? RadioGeddonRadioExternal : RadioGeddonRadioInternal)
#define radiogeddon_subghz_device_name  (radiogeddon_host->subghz_device_name)
#define radiogeddon_subghz_region_name  (radiogeddon_host->subghz_region_name)
#define radiogeddon_subghz_session_cost (radiogeddon_host->subghz_session_cost)
#define radiogeddon_scene_radio_cost    (radiogeddon_host->radio_cost)
#define radiogeddon_scene_show_report        (radiogeddon_host->show_report)
#define radiogeddon_scene_unknown_begin      (radiogeddon_host->unknown_begin)
#define radiogeddon_scene_unknown_end        (radiogeddon_host->unknown_end)
#define radiogeddon_scene_decoders_fit       (radiogeddon_host->decoders_fit)
#define radiogeddon_analysis_decode          (radiogeddon_host->analysis_decode)
#else
#define RADIOGEDDON_PRESET_LABEL(index) (radiogeddon_presets[(index)].label)
#endif
