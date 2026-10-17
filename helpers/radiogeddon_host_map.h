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
#define radiogeddon_ui_cancel                   (radiogeddon_host->ui_cancel)
#define radiogeddon_ui_set_scanning             (radiogeddon_host->ui_set_scanning)
#define radiogeddon_ui_apply_style              (radiogeddon_host->ui_apply_style)
#define radiogeddon_app_save_settings           (radiogeddon_host->save_settings)
#define radiogeddon_scene_build_list            (radiogeddon_host->build_list)
#define radiogeddon_memdiag_sample              (radiogeddon_host->memdiag_sample)
#define radiogeddon_scanner_alloc               (radiogeddon_host->scanner_alloc)
#define radiogeddon_scanner_configure           (radiogeddon_host->scanner_configure)
#define radiogeddon_scanner_set_callback        (radiogeddon_host->scanner_set_callback)
#define radiogeddon_scanner_set_mode            (radiogeddon_host->scanner_set_mode)
#define radiogeddon_scanner_set_observer        (radiogeddon_host->scanner_set_observer)
#define radiogeddon_scanner_start               (radiogeddon_host->scanner_start)
#define radiogeddon_scanner_stop                (radiogeddon_host->scanner_stop)
#define radiogeddon_scanner_lock                (radiogeddon_host->scanner_lock)
#define radiogeddon_scanner_unlock              (radiogeddon_host->scanner_unlock)
#define radiogeddon_scanner_reset_learning      (radiogeddon_host->scanner_reset_learning)
#define radiogeddon_scanner_reset_peaks         (radiogeddon_host->scanner_reset_peaks)
#define radiogeddon_subghz_is_device_present    (radiogeddon_host->subghz_is_device_present)
#define radiogeddon_subghz_set_preset           (radiogeddon_host->subghz_set_preset)
#define radiogeddon_subghz_set_frequency        (radiogeddon_host->subghz_set_frequency)
#define radiogeddon_subghz_is_frequency_allowed (radiogeddon_host->subghz_is_frequency_allowed)
#define radiogeddon_subghz_get_radio(subghz) \
    ((RadioGeddonRadio)radiogeddon_host->subghz_get_radio(subghz))
#define radiogeddon_subghz_scan_begin       (radiogeddon_host->subghz_scan_begin)
#define radiogeddon_subghz_scan_end         (radiogeddon_host->subghz_scan_end)
#define radiogeddon_subghz_probe_rssi_dwell (radiogeddon_host->subghz_probe_rssi_dwell)
#define radiogeddon_subghz_set_radio(subghz, radio, ext_power) \
    ((RadioGeddonRadio)radiogeddon_host->subghz_set_radio((subghz), (uint8_t)(radio), (ext_power)))
#define radiogeddon_subghz_device_name        (radiogeddon_host->subghz_device_name)
#define radiogeddon_subghz_region_name        (radiogeddon_host->subghz_region_name)
#define radiogeddon_settings_default_hop_mask (radiogeddon_host->settings_default_hop_mask)
#define radiogeddon_subghz_rx_start           (radiogeddon_host->subghz_rx_start)
#define radiogeddon_subghz_rx_stop            (radiogeddon_host->subghz_rx_stop)
#define radiogeddon_subghz_rx_retune          (radiogeddon_host->subghz_rx_retune)
#define radiogeddon_subghz_get_rssi           (radiogeddon_host->subghz_get_rssi)
#define radiogeddon_subghz_record_start(subghz) \
    ((RadioGeddonRecordError)radiogeddon_host->subghz_record_start(subghz))
#define radiogeddon_subghz_record_stop (radiogeddon_host->subghz_record_stop)
#define radiogeddon_subghz_record_status(subghz, stats) \
    (radiogeddon_host->subghz_record_status((subghz), (stats)))
#define radiogeddon_subghz_record_pending (radiogeddon_host->subghz_record_pending)
#define radiogeddon_subghz_record_save    (radiogeddon_host->subghz_record_save)
#define radiogeddon_subghz_record_discard (radiogeddon_host->subghz_record_discard)
#define radiogeddon_recorder_error_text(error) \
    (radiogeddon_host->recorder_error_text((uint8_t)(error)))
#define radiogeddon_scene_radio_memory_ok    (radiogeddon_host->radio_memory_ok)
#define radiogeddon_scene_radio_memory_learn (radiogeddon_host->radio_memory_learn)
#define radiogeddon_scene_receiver_badge     (radiogeddon_host->receiver_badge)
#define radiogeddon_ui_active                (radiogeddon_host->ui_active)
#define radiogeddon_ui_badge(app, frame, cyber) \
    (radiogeddon_host->ui_badge((app), (frame), (cyber)))
#define radiogeddon_scene_probe_bands     (radiogeddon_host->probe_bands)
#define radiogeddon_scene_plan_range(app) ((RgRangeResult)radiogeddon_host->plan_range(app))
#define radiogeddon_session_capture_saved (radiogeddon_host->session_capture_saved)
#define radiogeddon_session_active        (radiogeddon_host->session_active)
#define RADIOGEDDON_PRESET_LABEL(index)   (radiogeddon_host->preset_label(index))
#define radiogeddon_scene_show_report     (radiogeddon_host->show_report)
#define radiogeddon_scene_unknown_begin   (radiogeddon_host->unknown_begin)
#define radiogeddon_scene_unknown_end     (radiogeddon_host->unknown_end)
#define radiogeddon_scene_decoders_fit    (radiogeddon_host->decoders_fit)
#define radiogeddon_analysis_decode       (radiogeddon_host->analysis_decode)
#define radiogeddon_memdiag_stats            (radiogeddon_host->memdiag_stats)
#define radiogeddon_memdiag_diags            (radiogeddon_host->memdiag_diags)
#define radiogeddon_subghz_session_cost (radiogeddon_host->subghz_session_cost)
#define radiogeddon_scene_radio_cost    (radiogeddon_host->radio_cost)
#else
#define RADIOGEDDON_PRESET_LABEL(index) (radiogeddon_presets[(index)].label)
#endif
