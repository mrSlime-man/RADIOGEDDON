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
    m->anim = RgAnimNormal;
    rg_mascot_look_default(&m->look);
}

void rg_mascot_set_look(RgMascot* m, const RgMascotLook* look, uint8_t anim) {
    m->look = *look;
    rg_mascot_look_sanitize(&m->look);
    m->anim = anim < RgAnimCount ? anim : RgAnimNormal;
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
        // Back after a long quiet time: a short welcome.
        if(now - m->quiet_since >= RG_MASCOT_BORED_MS && (int32_t)(now - m->quiet_since) > 0) {
            m->greet_until = now + RG_MASCOT_GREET_MS;
            m->greet_live = 1;
        }
        break;
    case RgMascotEventAchievement:
    case RgMascotEventMatch:
    case RgMascotEventMilestone:
        m->celebrate_until = now + RG_MASCOT_CELEBRATE_MS;
        m->celebrate_live = 1;
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
    if(m->celebrate_live && rg_mascot_reached(now, m->celebrate_until)) m->celebrate_live = 0;
    if(m->greet_live && rg_mascot_reached(now, m->greet_until)) m->greet_live = 0;

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
    } else if(m->celebrate_live) {
        s = RgMascotStateCelebrate;
    } else if(m->saved_live) {
        s = RgMascotStateSaved;
    } else if(m->greet_live) {
        s = RgMascotStateGreet;
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
    f->phones = 1; // the default look (rg_mascot_style sets the chosen one)

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
    case RgMascotStateCelebrate:
        // A little dance: cheer with both ends, sparkles all round.
        f->eyes = RgMascotEyesHappy;
        f->fin = ((t / 300u) & 1u) ? RgMascotFinCheer : RgMascotFinPoint;
        f->fluke_flick = (uint8_t)((t / 200u) & 1u);
        f->body_dy = (uint8_t)((t / 300u) & 1u);
        f->phones_lift = (uint8_t)((t / 600u) & 1u);
        f->antenna = (uint8_t)(4u + (t / 150u) % 3u);
        f->fx = RgMascotFxSparkle;
        f->fx_phase = (uint8_t)((t / 120u) & 3u);
        f->fx_on = 1;
        break;
    case RgMascotStateGreet:
        // Wakes, blinks and waves.
        f->eyes = t < 300u ? RgMascotEyesBlink : RgMascotEyesHappy;
        f->fin = ((t / 400u) & 1u) ? RgMascotFinCheer : RgMascotFinRest;
        f->goggles = 1;
        f->eye_dx = 1;
        break;
    default:
        f->state = RgMascotStateIdle;
        break;
    }
}

void rg_mascot_look_default(RgMascotLook* look) {
    memset(look, 0, sizeof(*look));
    look->phones = 1;
}

void rg_mascot_look_sanitize(RgMascotLook* look) {
    if(look->phones > 1) look->phones = 1;
    if(look->goggles >= RgLookGogglesCount) look->goggles = RgLookGogglesAuto;
    if(look->antenna >= RgLookAntennaCount) look->antenna = RgLookAntennaClassic;
    if(look->tool >= RgLookToolCount) look->tool = RgLookToolNone;
    if(look->expression >= RgLookExpressionCount) look->expression = RgLookExpressionNormal;
    if(look->accessory >= RgLookAccessoryCount) look->accessory = RgLookAccessoryNone;
}

void rg_mascot_style(RgMascotFrame* f, const RgMascotLook* look, uint8_t anim, uint32_t t) {
    f->phones = look->phones ? 1 : 0;
    f->goggles_mode = look->goggles < RgLookGogglesCount ? look->goggles : 0;
    f->antenna_kind = look->antenna < RgLookAntennaCount ? look->antenna : 0;
    f->tool = look->tool < RgLookToolCount ? look->tool : 0;
    f->accessory = look->accessory < RgLookAccessoryCount ? look->accessory : 0;
    if(f->goggles_mode == RgLookGogglesAlways) f->goggles = 1;
    if(f->goggles_mode == RgLookGogglesOff) f->goggles = 0;
    // Expressions colour the calm states only; reactions keep their eyes.
    bool calm = f->state == RgMascotStateIdle || f->state == RgMascotStateBored;
    if(calm && f->eyes == RgMascotEyesOpen) {
        if(look->expression == RgLookExpressionCheerful) f->eyes = RgMascotEyesHappy;
        if(look->expression == RgLookExpressionFocused) f->eyes = RgMascotEyesFocus;
    }
    switch(anim) {
    case RgAnimOff:
        // One still pose per state: nothing moves, so nothing is redrawn.
        f->body_dy = 0;
        f->fluke_flick = 0;
        f->prop_phase = 0;
        f->fx_phase = 0;
        if(f->eyes == RgMascotEyesBlink) f->eyes = RgMascotEyesOpen;
        if(calm) {
            f->eye_dx = 0;
            f->eye_dy = 0;
            f->prop = RgMascotPropNone;
            f->fin = RgMascotFinRest;
        }
        if(f->state == RgMascotStateAnalyzing) f->fin = RgMascotFinRest;
        if(f->state == RgMascotStateCelebrate || f->state == RgMascotStateGreet)
            f->fin = RgMascotFinCheer;
        if(f->state == RgMascotStateCelebrate) {
            f->antenna = 4;
            f->phones_lift = 0;
        }
        if(f->state == RgMascotStateScanning) {
            f->antenna = 4;
            f->eye_dy = 0;
            f->eye_dx = 1;
            f->eyes = RgMascotEyesOpen;
            f->fin = RgMascotFinRest;
        }
        // Information stays visible, steadily.
        if(f->fx != RgMascotFxNone) f->fx_on = 1;
        break;
    case RgAnimMinimal:
        f->body_dy = 0;
        f->fluke_flick = 0;
        if(calm) {
            f->eye_dx = 0;
            f->eye_dy = 0;
        }
        f->prop_phase = (uint8_t)(f->prop_phase & 0x0Cu); // a quarter of the steps
        break;
    case RgAnimFull: {
        // The antenna sways and the tail answers now and then.
        bool sway = f->state == RgMascotStateIdle || f->state == RgMascotStateScanning ||
                    f->state == RgMascotStateReceiving;
        if(sway && f->antenna >= 4 && ((t / 400u) & 3u) == 1u) f->antenna = (uint8_t)(f->antenna + 1u);
        if(f->state == RgMascotStateIdle && (t / 700u) % 7u == 3u) f->fluke_flick = 1;
        if(f->state == RgMascotStateReceiving && f->eyes == RgMascotEyesOpen)
            f->eye_dx = (int8_t)(((t / 900u) & 1u) ? 1 : 0);
        break;
    }
    default:
        break;
    }
}

const char* rg_anim_level_name(RgAnimLevel level) {
    static const char* const names[RgAnimCount] = {"Off", "Minimal", "Normal", "Full"};
    return (unsigned)level < RgAnimCount ? names[level] : "?";
}

void rg_mascot_frame(RgMascot* m, uint32_t now, RgMascotFrame* out) {
    RgMascotState s = rg_mascot_state(m, now);
    bool flash = m->detect_live && (s == RgMascotStateReceiving || s == RgMascotStateRecording);
    uint32_t t = now - m->state_since;
    rg_mascot_pose(s, t, m->seed, m->progress, flash, out);
    rg_mascot_style(out, &m->look, m->anim, t);
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
        "Celebrate",
        "Welcome back",
    };
    return (unsigned)state < RgMascotStateCount ? names[state] : "?";
}

#endif
