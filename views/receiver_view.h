#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Live radio screen shared by the Receiver, Frequency Hopper and RAW Record
 * scenes. It owns only presentation state; the scenes drive the radio and
 * push updates in. All setters are safe to call from the radio worker thread
 * (they take the view-model lock).
 */

typedef enum {
    RgReceiverModeReceiver,
    RgReceiverModeHopper,
    RgReceiverModeRecord,
} RgReceiverMode;

typedef struct RgReceiverView RgReceiverView;

/** Fired on OK press (used by Record mode to stop + save). */
typedef void (*RgReceiverViewOkCallback)(void* context);

RgReceiverView* rg_receiver_view_alloc(void);
void rg_receiver_view_free(RgReceiverView* v);
View* rg_receiver_view_get_view(RgReceiverView* v);

void rg_receiver_view_set_mode(RgReceiverView* v, RgReceiverMode mode);
void rg_receiver_view_set_radio_ok(RgReceiverView* v, bool ok);
void rg_receiver_view_set_frequency(RgReceiverView* v, uint32_t frequency);
void rg_receiver_view_set_rssi(RgReceiverView* v, float rssi);
void rg_receiver_view_set_status(RgReceiverView* v, const char* status);
void rg_receiver_view_set_samples(RgReceiverView* v, uint32_t samples);
void rg_receiver_view_set_detections(RgReceiverView* v, uint32_t count);
void rg_receiver_view_set_last_protocol(RgReceiverView* v, const char* proto, bool rolling);
void rg_receiver_view_reset(RgReceiverView* v);

void rg_receiver_view_set_ok_callback(
    RgReceiverView* v,
    RgReceiverViewOkCallback callback,
    void* context);

#ifdef __cplusplus
}
#endif
