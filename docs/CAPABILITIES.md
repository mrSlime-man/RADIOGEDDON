# Implemented and Unimplemented Capabilities

This document tracks exactly what RadioGeddon does and does not do, to avoid
overstating functionality. Last updated for v0.2 (post-stabilization pass).

For the exact verification status (what is build/test-verified vs. what needs a
physical device), see [`VERIFICATION.md`](VERIFICATION.md) and the test plan in
[`HARDWARE_CHECKLIST.md`](HARDWARE_CHECKLIST.md).

## Implemented

### Core / platform
- [x] Native Flipper Zero GUI (ViewDispatcher + SceneManager), physical-button
      navigation only.
- [x] Builds as a `.fap` with `ufbt` against the official release SDK
      (firmware 1.4.3, API 87.1), zero warnings.
- [x] Persistent storage on the SD card under
      `/ext/apps_data/radiogeddon/signals`, using the standard `.sub` format.
- [x] Graceful handling when no radio device is detected (clear message instead
      of fake data).
- [x] Modular architecture; protocol identification delegated to the firmware's
      pluggable decoder registry.

### Radio modules
- [x] **Scanner** — real per-frequency RSSI read from the CC1101 across a table
      of common Sub-GHz frequencies, with a live bar display.
- [x] **Receiver** — live decoding of all firmware-supported "decodable"
      protocols (Princeton, CAME, NICE, KeeLoq-family, Holtek, etc.), live RSSI
      meter, de-duplicated decoded-signal history.
- [x] **RAW Recorder** — captures the real incoming timing stream to a RAW
      `.sub` file (buffered in RAM, flushed to disk on stop).
- [x] **Save decoded signal** — serializes a decoded protocol parcel to a
      loadable `.sub` file.
- [x] **Database** — browse / open / delete saved recordings via the firmware
      file browser.
- [x] **Reopen & display** — parses a saved `.sub` (RAW or protocol) and shows
      frequency, preset, protocol, bit length, key, sample count, pulse range.
- [x] **Signal Analyzer** — RAW pulse-width clustering with estimated base `Te`
      and edge count; bit/field breakdown for decoded protocols.
- [x] **Crypto Analyzer** — static vs. dynamic (rolling/encrypted)
      classification from the confirmed protocol type, plus a key-byte variety
      heuristic. Clearly labelled; no key recovery.
- [x] **Comparator** — field-by-field diff of two recordings (protocol,
      frequency, key, sample counts) with a rolling-counter delta hint.
- [x] **Replay / TX** — transmits RAW and static-protocol `.sub` files for
      authorized testing; refuses dynamic/rolling-code protocols; relies on
      firmware region enforcement.
- [x] Clear `[CONFIRMED]` vs `[HEURISTIC]` labelling throughout.

### Testing
- [x] Host unit tests for the firmware-independent parsing/clustering/key-stat
      logic (`test/`, run with `make -C test check`).

## Not yet implemented / out of scope

- [ ] **On-device hardware verification.** The milestone flow has not yet been
      confirmed on a physical Flipper (see `LIMITATIONS.md`).
- [ ] Frequency hopper (continuous multi-frequency receive).
- [ ] External CC1101 module auto-selection UI (code paths use the generic
      device API but only the internal radio is wired into the menus).
- [ ] Editing/renaming saved signals in place.
- [ ] Manual/custom frequency and custom-preset register entry (fixed table +
      four standard presets for now).
- [ ] Weather-station / sensor-specific decoders beyond what the firmware
      registry provides.
- [ ] Brute-force, key recovery, rolling-code defeat, or any attack tooling —
      intentionally excluded.
- [ ] Desktop companion application (explicitly deferred; file format kept
      compatible to enable it later).

## Milestone definition

> The initial milestone is complete when RadioGeddon can receive an actual
> supported Sub-GHz transmission, save the recording, display basic signal
> information, and reopen the recording directly on Flipper Zero.

All four steps are **implemented in code and build cleanly**. Marking the
milestone *verified* requires the on-device test described in `LIMITATIONS.md`.
