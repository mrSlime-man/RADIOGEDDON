# Roadmap

Where RadioGeddon is headed. This is a plan, not a promise — priorities shift
with what hardware testing finds and what contributors pick up. Dates are
deliberately omitted.

## Now — `1.0.0-beta.2`

`1.0.0-beta.2` fixes an out-of-memory crash on launch that made `1.0.0-beta.1`
unusable on RogueMaster. The full toolkit is implemented and builds for Official, Unleashed and
RogueMaster, with 364 host-test checks and a verified release pipeline. See
[Features](FEATURES.md) for the complete list.

## Next: hardware verification (the gating milestone)

Nothing advances to a stable `1.0.0` until the app is exercised on real
hardware. The goal is to work through [the checklist](HARDWARE_CHECKLIST.md) on
all three firmware families and record evidence in
[VERIFICATION.md](VERIFICATION.md).

- [ ] End-to-end workflow (W1–W10) confirmed on at least one firmware.
- [ ] Each per-firmware build confirmed to load on its firmware (L1–L3).
- [ ] Receive/decode, RAW capture fidelity, and authorized replay confirmed
      against real remotes.
- [ ] Regional transmit enforcement confirmed to block disallowed frequencies.
- [ ] A memory-stability pass over repeated capture/analyse/replay cycles.

This is where help is most valuable — see [Contributing](../CONTRIBUTING.md).

## Development milestones

Work proceeds one milestone at a time, each through focused pull requests with
host tests, all three firmware builds, and a list of the hardware checks it
still needs.

- [x] **0. Working baseline**: launch out-of-memory crash fixed, `1.0.0-beta.2`.
- [ ] **1. Advanced radio scanner**: noise floor, thresholds, peak hold,
      activity counts, scan lists, dwell, hold on hit, CSV export
      (implemented; hardware checks F1a–F1e pending).
- [ ] **2. Frequency Hopper 2.0**: custom lists, dwell, adaptive thresholds,
      pause/lock, detection history, optional auto-recording
      (implemented; hardware checks F2a–F2e pending).
- [ ] **3. Signal Analyzer 2.0**: whole-file streaming analysis, PWM/PPM/
      Manchester trial decoding with confidence, noise and jitter, frame
      grouping, alignment and comparison, and a zoomable pulse timeline
      (implemented; hardware checks F4a–F4e pending).
- [ ] **4. Streaming RAW recording**: bounded RAM, writes to SD while
      recording on its own thread, loss counting and reporting, duration and
      sample counter, save result, cancel (implemented; hardware checks
      F6–F6e pending).
- [ ] **5. Signal Database 2.0**: rename, sort, filter, metadata, duplicates,
      report export, corrupted-file handling.
- [ ] **6. Interface pass**: consistent layout, status, errors, shortcuts.
- [ ] **7. Optional external CC1101** through the firmware's device layer.
- [ ] **8. Performance and reliability**: memory diagnostics, lifecycle tests.

## Toward a stable 1.0.0

- [ ] Fix whatever hardware testing surfaces.
- [x] Persist Settings (frequency, modulation, scanner options) across launches.
- [ ] Save analysis reports to the SD card as text.
- [ ] Rename recordings from the Database screen.

## Later

- [ ] External CC1101 module support surfaced in the UI (the radio layer
      already uses the portable device API).
- [ ] Custom frequency entry and a custom modulation/preset editor.
- [ ] Richer analysis: more encodings, CRC/checksum guesses, bit-field views,
      running the firmware's decoders over a RAW capture.
- [ ] A larger library of reference captures for regression testing.
- [ ] Localisation of on-screen text.

## Under consideration (not committed)

- A desktop companion that imports these `.sub` recordings for heavier,
  off-device analysis. The on-device file format is kept standard so this stays
  possible; it is not being built as part of this project yet.

## Explicitly out of scope

RadioGeddon will not add key recovery, rolling-code prediction or bypass,
brute-forcing, jamming, or any removal of the transmit safeguards. These are
non-goals by design — see the [Security Policy](../SECURITY.md).

## Suggesting a direction

Open a [feature request](https://github.com/mrSlime-man/RADIOGEDDON/issues/new?template=feature_request.yml)
or start a discussion. Contributions that move roadmap items forward are
welcome.
