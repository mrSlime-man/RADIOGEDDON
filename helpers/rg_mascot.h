/**
 * @file rg_mascot.h
 * @brief Dolphin Lab: the mascot's state machine (Full edition).
 *
 * The app reports what really happens (a scan runs, the receiver hears a
 * signal, a recording starts, an analysis reports progress, a save
 * completes, an operation fails, memory runs low) and the user's key
 * presses; this module turns them into one state and, for any moment, the
 * pose to draw (RgMascotFrame). It never touches the radio and never
 * decides anything about it: radio work goes on whatever the mascot shows.
 *
 * Ongoing activity (scanning, receiving, recording, analysing, low memory)
 * is a level the app sets from the real state on every tick; reactions
 * (a detection, a save, an error) are a deadline, so repeated events only
 * move the deadline and nothing queues up. When several apply, the most
 * important wins:
 *
 *   Error > Low memory > Recording > Receiving > Signal detected >
 *   Scanning > Analyzing > Saved > Asleep > Bored > Idle
 *
 * A detection while receiving or recording still shows as a short "!" on
 * the higher state's pose. Bored and asleep follow a long time without a
 * key press or an event; they are never read as a radio fault.
 *
 * Pure C with no SDK headers; times are milliseconds from any monotonic
 * clock (wrap-around safe). Host-tested in test/test_mascot.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RgMascotStateIdle,
    RgMascotStateBored, // a while without input or events
    RgMascotStateAsleep, // a long while: the "no signal" doze
    RgMascotStateSaved, // a save completed (short)
    RgMascotStateAnalyzing,
    RgMascotStateScanning,
    RgMascotStateDetected, // activity seen (short)
    RgMascotStateReceiving,
    RgMascotStateRecording,
    RgMascotStateLowMemory,
    RgMascotStateError, // an operation failed (short)
    RgMascotStateCount,
} RgMascotState;

typedef enum {
    RgMascotEventScanStarted,
    RgMascotEventScanStopped,
    RgMascotEventSignalActivity,
    RgMascotEventRxStarted,
    RgMascotEventRxStopped,
    RgMascotEventRecordStarted,
    RgMascotEventRecordStopped,
    RgMascotEventAnalysisStarted,
    RgMascotEventAnalysisProgress, // arg: percent 0..100
    RgMascotEventAnalysisCompleted,
    RgMascotEventSaveCompleted,
    RgMascotEventOperationFailed,
    RgMascotEventLowMemory,
    RgMascotEventMemoryOk,
    RgMascotEventUserInput,
    RgMascotEventCount,
} RgMascotEvent;

/* How long a reaction shows (ms). */
#define RG_MASCOT_DETECT_MS 1500u
#define RG_MASCOT_SAVED_MS  2000u
#define RG_MASCOT_ERROR_MS  3000u
/* Quiet time before the mascot gets bored, then falls asleep (ms). */
#define RG_MASCOT_BORED_MS  45000u
#define RG_MASCOT_ASLEEP_MS 120000u

/* Progress not known (an analysis that does not report it). */
#define RG_MASCOT_PROGRESS_NONE 0xFFu

/* Eyes (RgMascotFrame.eyes). */
typedef enum {
    RgMascotEyesOpen,
    RgMascotEyesBlink,
    RgMascotEyesHappy,
    RgMascotEyesWide,
    RgMascotEyesWorried,
    RgMascotEyesSleepy,
    RgMascotEyesShut,
    RgMascotEyesFocus,
    RgMascotEyesCount,
} RgMascotEyes;

typedef enum {
    RgMascotFinRest,
    RgMascotFinPoint,
    RgMascotFinCheer,
    RgMascotFinCount,
} RgMascotFin;

/* What the dolphin works with, drawn beside it. */
typedef enum {
    RgMascotPropNone,
    RgMascotPropRadio, // a handheld radio it examines (idle)
    RgMascotPropRadar, // a radar it watches (scanning)
    RgMascotPropScope, // an oscilloscope (detection, receiving)
    RgMascotPropRecorder, // a reel recorder (recording)
    RgMascotPropComputer, // a terminal (analysing)
    RgMascotPropCount,
} RgMascotProp;

/* An effect over the scene. */
typedef enum {
    RgMascotFxNone,
    RgMascotFxExclaim, // "!" bubble: a signal
    RgMascotFxQuestion, // "?" bubble: an error
    RgMascotFxZzz, // asleep
    RgMascotFxSparkle, // a save
    RgMascotFxWaves, // sound in the headphones
    RgMascotFxRec, // the REC lamp (recording)
    RgMascotFxChip, // the memory chip (low memory)
    RgMascotFxCount,
} RgMascotFx;

/**
 * One pose: what to draw. Only one-byte fields, so two frames compare with
 * memcmp (a redraw is asked for only when the frame changed).
 */
typedef struct {
    uint8_t state; // RgMascotState the pose shows
    uint8_t eyes; // RgMascotEyes
    int8_t eye_dx; // look offset, -1..1
    int8_t eye_dy;
    uint8_t fin; // RgMascotFin
    uint8_t fluke_flick; // tail flicked up
    uint8_t body_dy; // 0/1: breathing
    uint8_t phones_lift; // headphones raised off the head (pixels)
    uint8_t goggles; // goggles on the forehead
    uint8_t antenna; // antenna length, 3..6 pixels
    uint8_t prop; // RgMascotProp
    uint8_t prop_phase; // 0..15, the prop's animation step
    uint8_t fx; // RgMascotFx
    uint8_t fx_phase; // 0..3
    uint8_t fx_on; // the effect is visible in this frame (blinking)
    uint8_t progress; // 0..100, or RG_MASCOT_PROGRESS_NONE
} RgMascotFrame;

typedef struct {
    uint32_t now; // time of the last event or update
    uint32_t quiet_since; // last key press or event
    uint32_t detect_until;
    uint32_t saved_until;
    uint32_t error_until;
    uint32_t state_since; // when the shown state began
    uint32_t seed; // idle variation
    uint32_t detections; // activity events seen (saturating)
    uint8_t state; // RgMascotState shown since state_since
    uint8_t scanning;
    uint8_t receiving;
    uint8_t recording;
    uint8_t analyzing;
    uint8_t low_memory;
    uint8_t progress; // RG_MASCOT_PROGRESS_NONE when unknown
    uint8_t detect_live; // a detection deadline is set
    uint8_t saved_live;
    uint8_t error_live;
} RgMascot;

/** Start idle at @p now; @p seed varies the idle behaviour. */
void rg_mascot_init(RgMascot* m, uint32_t now, uint32_t seed);

/** Report an event at @p now. Unknown events are ignored. */
void rg_mascot_event(RgMascot* m, RgMascotEvent event, uint32_t arg, uint32_t now);

/**
 * Set the ongoing activities from the real state (call on every tick):
 * each changed level is reported as its start or stop event.
 */
void rg_mascot_sync(
    RgMascot* m,
    bool scanning,
    bool receiving,
    bool recording,
    bool low_memory,
    uint32_t now);

/** The state shown at @p now (expired reactions are dropped). */
RgMascotState rg_mascot_state(RgMascot* m, uint32_t now);

/** The pose at @p now. */
void rg_mascot_frame(RgMascot* m, uint32_t now, RgMascotFrame* out);

/**
 * The pose of @p state @p t ms after it began, for a gallery or a card
 * that shows a given state. @p progress as in RgMascotFrame.
 */
void rg_mascot_pose(
    RgMascotState state,
    uint32_t t,
    uint32_t seed,
    uint8_t progress,
    bool detect_flash,
    RgMascotFrame* out);

/** True if the two frames draw differently. */
bool rg_mascot_frame_changed(const RgMascotFrame* a, const RgMascotFrame* b);

/** Short name of a state ("Idle", "Scanning", ...). */
const char* rg_mascot_state_name(RgMascotState state);
