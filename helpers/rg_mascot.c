#include "rg_mascot.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_DOLPHIN_LAB

#include <string.h>

/* True once @p deadline is reached (wrap-around safe). */
static bool rg_mascot_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t rg_mascot_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void rg_mascot_init(RgMascot* m, uint32_t now, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->now = now;
    m->quiet_since = now;
    m->state_since = now;
    m->seed = seed;
    m->state = RgMascotStateIdle;
    m->progress = RG_MASCOT_PROGRESS_NONE;
}

void rg_mascot_event(RgMascot* m, RgMascotEvent event, uint32_t arg, uint32_t now) {
    m->now = now;
    switch(event) {
    case RgMascotEventScanStarted:
        m->scanning = 1;
        break;
    case RgMascotEventScanStopped:
        m->scanning = 0;
        break;
    case RgMascotEventSignalActivity:
        m->detect_until = now + RG_MASCOT_DETECT_MS;
        m->detect_live = 1;
        if(m->detections != UINT32_MAX) m->detections++;
        break;
    case RgMascotEventRxStarted:
        m->receiving = 1;
        break;
    case RgMascotEventRxStopped:
        m->receiving = 0;
        break;
    case RgMascotEventRecordStarted:
        m->recording = 1;
        break;
    case RgMascotEventRecordStopped:
        m->recording = 0;
        break;
    case RgMascotEventAnalysisStarted:
        m->analyzing = 1;
        m->progress = RG_MASCOT_PROGRESS_NONE;
        break;
    case RgMascotEventAnalysisProgress:
        m->analyzing = 1;
        m->progress = (uint8_t)(arg > 100 ? 100 : arg);
        break;
    case RgMascotEventAnalysisCompleted:
        m->analyzing = 0;
        m->progress = RG_MASCOT_PROGRESS_NONE;
        break;
    case RgMascotEventSaveCompleted:
        m->saved_until = now + RG_MASCOT_SAVED_MS;
        m->saved_live = 1;
        break;
    case RgMascotEventOperationFailed:
        m->error_until = now + RG_MASCOT_ERROR_MS;
        m->error_live = 1;
        break;
    case RgMascotEventLowMemory:
        m->low_memory = 1;
        break;
    case RgMascotEventMemoryOk:
        m->low_memory = 0;
        break;
    case RgMascotEventUserInput:
        break;
    default:
        return; // unknown: ignored, not even counted as activity
    }
    m->quiet_since = now;
}

void rg_mascot_sync(
    RgMascot* m,
    bool scanning,
    bool receiving,
    bool recording,
    bool low_memory,
    uint32_t now) {
    if(scanning != (m->scanning != 0))
        rg_mascot_event(m, scanning ? RgMascotEventScanStarted : RgMascotEventScanStopped, 0, now);
    if(receiving != (m->receiving != 0))
        rg_mascot_event(m, receiving ? RgMascotEventRxStarted : RgMascotEventRxStopped, 0, now);
    if(recording != (m->recording != 0))
        rg_mascot_event(
            m, recording ? RgMascotEventRecordStarted : RgMascotEventRecordStopped, 0, now);
    if(low_memory != (m->low_memory != 0))
        rg_mascot_event(m, low_memory ? RgMascotEventLowMemory : RgMascotEventMemoryOk, 0, now);
    // Radio work going on is not a quiet lab.
    if(scanning || receiving || recording) m->quiet_since = now;
    m->now = now;
}

RgMascotState rg_mascot_state(RgMascot* m, uint32_t now) {
    if(m->detect_live && rg_mascot_reached(now, m->detect_until)) m->detect_live = 0;
    if(m->saved_live && rg_mascot_reached(now, m->saved_until)) m->saved_live = 0;
    if(m->error_live && rg_mascot_reached(now, m->error_until)) m->error_live = 0;

    RgMascotState s;
    if(m->error_live) {
        s = RgMascotStateError;
    } else if(m->low_memory) {
        s = RgMascotStateLowMemory;
    } else if(m->recording) {
        s = RgMascotStateRecording;
    } else if(m->receiving) {
        s = RgMascotStateReceiving;
    } else if(m->detect_live) {
        s = RgMascotStateDetected;
    } else if(m->scanning) {
        s = RgMascotStateScanning;
    } else if(m->analyzing) {
        s = RgMascotStateAnalyzing;
    } else if(m->saved_live) {
        s = RgMascotStateSaved;
    } else {
        uint32_t quiet = now - m->quiet_since;
        // A clock that went backwards is not a long quiet time.
        if((int32_t)quiet < 0) quiet = 0;
        s = quiet >= RG_MASCOT_ASLEEP_MS ? RgMascotStateAsleep :
            quiet >= RG_MASCOT_BORED_MS  ? RgMascotStateBored :
                                           RgMascotStateIdle;
    }
    if(s != m->state) {
        m->state = (uint8_t)s;
        m->state_since = now;
    }
    m->now = now;
    return s;
}

static void rg_mascot_pose_idle(uint32_t t, uint32_t seed, RgMascotFrame* f) {
    f->body_dy = (uint8_t)((t / 1200u) & 1u);
    f->goggles = 1;
    uint32_t slot = t / 4000u;
    uint32_t within = t % 4000u;
    uint32_t action = rg_mascot_hash(seed ^ (slot * 0x9e3779b9U)) % 6u;
    switch(action) {
    case 1: // looks around the lab
        if(within < 1000u) {
            f->eye_dx = -1;
        } else if(within < 2000u) {
            f->eye_dx = 1;
        }
        break;
    case 2: // tail flick
        if(within < 1500u) f->fluke_flick = (uint8_t)((within / 250u) & 1u);
        break;
    case 3: // examines a handheld radio
        if(within < 3000u) {
            f->prop = RgMascotPropRadio;
            f->eye_dx = 1;
            f->eye_dy = 1;
            f->fin = RgMascotFinPoint;
            f->prop_phase = (uint8_t)((within / 500u) & 15u);
        }
        break;
    case 4: // glances up at the antenna
        if(within < 1200u) f->eye_dy = -1;
        break;
    default:
        break;
    }
    // A blink near the end of each slot (two 100 ms ticks see it).
    if(action != 3 && within >= 3300u && within < 3500u) f->eyes = RgMascotEyesBlink;
}

void rg_mascot_pose(
    RgMascotState state,
    uint32_t t,
    uint32_t seed,
    uint8_t progress,
    bool detect_flash,
    RgMascotFrame* out) {
    RgMascotFrame* f = out;
    memset(f, 0, sizeof(*f));
    f->state = (uint8_t)state;
    f->eyes = RgMascotEyesOpen;
    f->fin = RgMascotFinRest;
    f->antenna = 4;
    f->progress = RG_MASCOT_PROGRESS_NONE;

    switch(state) {
    case RgMascotStateIdle:
        rg_mascot_pose_idle(t, seed, f);
        break;
    case RgMascotStateBored:
        f->eyes = RgMascotEyesSleepy;
        f->body_dy = (uint8_t)((t / 1600u) & 1u);
        f->goggles = 1;
        // Now and then a slow look aside.
        if(t % 8000u >= 6000u) f->eye_dx = -1;
        break;
    case RgMascotStateAsleep:
        f->eyes = RgMascotEyesShut;
        f->body_dy = (uint8_t)((t / 2000u) & 1u);
        f->antenna = 3;
        f->fx = RgMascotFxZzz;
        f->fx_phase = (uint8_t)((t / 500u) & 3u);
        f->fx_on = 1;
        break;
    case RgMascotStateSaved:
        f->eyes = RgMascotEyesHappy;
        f->fin = RgMascotFinCheer;
        f->fluke_flick = (uint8_t)((t / 250u) & 1u);
        f->body_dy = (uint8_t)((t / 250u) & 1u);
        f->fx = RgMascotFxSparkle;
        f->fx_phase = (uint8_t)((t / 150u) & 3u);
        f->fx_on = 1;
        break;
    case RgMascotStateAnalyzing:
        f->eyes = RgMascotEyesFocus;
        f->eye_dx = 1;
        f->goggles = 1;
        f->prop = RgMascotPropComputer;
        f->prop_phase = (uint8_t)((t / 200u) & 15u);
        f->fin = ((t / 250u) & 1u) ? RgMascotFinPoint : RgMascotFinRest; // typing
        f->progress = progress;
        break;
    case RgMascotStateScanning: {
        f->eye_dx = 1;
        f->prop = RgMascotPropRadar;
        f->prop_phase = (uint8_t)((t / 125u) & 15u);
        uint32_t within = t % 6000u;
        if(within >= 2000u && within < 2600u) {
            // Adjusts the antenna.
            f->fin = RgMascotFinCheer;
            f->antenna = (uint8_t)(4u + ((within - 2000u) / 200u) % 3u);
            f->eye_dx = 0;
            f->eye_dy = -1;
        } else if(within >= 4000u && within < 4800u) {
            // Checks the signal monitor.
            f->eye_dx = 0;
            f->eye_dy = -1;
        } else if(within >= 5200u && within < 5400u) {
            f->eyes = RgMascotEyesBlink;
        }
        break;
    }
    case RgMascotStateDetected:
        f->eyes = RgMascotEyesWide;
        f->phones_lift = 3;
        f->fin = RgMascotFinPoint;
        f->fluke_flick = t < 300u ? 1 : 0;
        f->prop = RgMascotPropScope;
        f->prop_phase = (uint8_t)((t / 125u) & 15u);
        f->fx = RgMascotFxExclaim;
        f->fx_on = (uint8_t)(((t / 250u) & 1u) == 0);
        break;
    case RgMascotStateReceiving:
        f->eyes = ((t / 2000u) % 3u == 2u) ? RgMascotEyesOpen : RgMascotEyesShut;
        f->body_dy = (uint8_t)((t / 1200u) & 1u);
        f->fx = RgMascotFxWaves;
        f->fx_phase = (uint8_t)((t / 250u) % 3u);
        f->fx_on = 1;
        if(detect_flash) {
            f->eyes = RgMascotEyesWide;
            f->phones_lift = 3;
            f->fin = RgMascotFinPoint;
            f->fx = RgMascotFxExclaim;
            f->fx_on = (uint8_t)(((t / 250u) & 1u) == 0);
        }
        break;
    case RgMascotStateRecording:
        f->eyes = RgMascotEyesFocus;
        f->eye_dx = 1;
        f->eye_dy = 1;
        f->fin = RgMascotFinPoint;
        f->prop = RgMascotPropRecorder;
        f->prop_phase = (uint8_t)((t / 125u) & 15u);
        // The REC label always shows; only its lamp blinks.
        f->fx = RgMascotFxRec;
        f->fx_on = (uint8_t)(((t / 500u) & 1u) == 0);
        if(detect_flash) {
            f->eyes = RgMascotEyesWide;
            f->phones_lift = 2;
        }
        break;
    case RgMascotStateLowMemory:
        f->eyes = RgMascotEyesWorried;
        f->eye_dx = -1;
        f->antenna = 3;
        f->fx = RgMascotFxChip;
        f->fx_on = (uint8_t)(((t / 600u) & 1u) == 0);
        break;
    case RgMascotStateError:
        f->eyes = RgMascotEyesWorried;
        f->antenna = 3;
        f->fx = RgMascotFxQuestion;
        f->fx_phase = (uint8_t)((t / 400u) & 1u);
        f->fx_on = 1;
        break;
    default:
        f->state = RgMascotStateIdle;
        break;
    }
}

void rg_mascot_frame(RgMascot* m, uint32_t now, RgMascotFrame* out) {
    RgMascotState s = rg_mascot_state(m, now);
    bool flash = m->detect_live && (s == RgMascotStateReceiving || s == RgMascotStateRecording);
    rg_mascot_pose(s, now - m->state_since, m->seed, m->progress, flash, out);
}

bool rg_mascot_frame_changed(const RgMascotFrame* a, const RgMascotFrame* b) {
    return memcmp(a, b, sizeof(*a)) != 0;
}

const char* rg_mascot_state_name(RgMascotState state) {
    static const char* const names[RgMascotStateCount] = {
        "Idle",
        "Bored",
        "Asleep",
        "Saved",
        "Analyzing",
        "Scanning",
        "Signal!",
        "Receiving",
        "Recording",
        "Low memory",
        "Error",
    };
    return (unsigned)state < RgMascotStateCount ? names[state] : "?";
}

#endif
