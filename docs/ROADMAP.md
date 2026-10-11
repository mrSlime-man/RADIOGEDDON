# Roadmap

Where RadioGeddon is headed. This is a plan, not a promise — priorities shift
with what hardware testing finds and what contributors pick up. Dates are
deliberately omitted.

## Now — `1.0.0-beta.7`

`1.0.0-beta.7` adds research tools to the Full edition: a **waterfall** (RSSI
sweep history of the Range Scanner), the **Bitstream Explorer**,
**Multi-Capture Compare** and **Research Sessions**, loaded as modules only
while in use so the resident app grows by 9.7 KB instead of 33.6 KB, and a
main menu grouped into Scan, Receive & Record, Analyze, Database and Sessions.
Unknown Protocol Analysis tells pulse-width codes from Manchester by how
pulses pair with gaps (encoding right on 43 of the firmware's 50 test
captures, frame length on 35), and the screens are tested on a host canvas
with the firmware's fonts. None of it is verified on hardware yet.

`1.0.0-beta.6` splits RadioGeddon into two editions from one source tree:
**Full** for RogueMaster, Momentum (its first build) and Unleashed, with a
range scanner, scan profiles, favorites, fine stepping and checksum
hypotheses; and **Catalog** for Official firmware, which checks the
firmware's region before every transmission and is being prepared for the
Flipper Apps Catalog. Unknown Protocol Analysis gets Te right on all 50 of the
firmware's test captures. None of it is verified on hardware yet.

`1.0.0-beta.5` fixed keys saved from Receive and the Hopper, which the stock
Sub-GHz app could not open, and has Replay check a file's custom preset before
loading it into the radio. Unknown Protocol Analysis is now scored on the
firmware's own RAW test captures and reads them better, and Settings takes a
typed frequency. None of it is verified on hardware yet.

`1.0.0-beta.4` fixed a crash in Receive and the Hopper on Official firmware
(CAME Atomo and Alutech AT-4N decodes) and adds *Decode with Firmware*, which
runs the firmware's own decoders over a saved RAW capture. Neither is verified
on hardware yet.

`1.0.0-beta.3` brought development milestones 1 to 9 below: the upgraded
scanner and hopper, streaming recording, Analyzer 2.0 with the Pulse Timeline,
Database 2.0, the interface pass, the optional external CC1101, memory
diagnostics, and the testing behind them (532 host-test checks, format tests,
fuzzing and static analysis in CI). It builds for Official, Unleashed and
RogueMaster with a verified release pipeline. None of it is verified on
hardware yet. `1.0.0-beta.2` fixed the out-of-memory crash on launch that
made `1.0.0-beta.1` unusable on RogueMaster.

## Next: hardware verification (the gating milestone)

Nothing advances to a stable `1.0.0` until the app is exercised on real
hardware. The goal is to work through [the checklist](HARDWARE_CHECKLIST.md) on
all four firmware families and both editions and record evidence in
[VERIFICATION.md](VERIFICATION.md).

- [ ] End-to-end workflow (W1–W10) confirmed on at least one firmware.
- [ ] Each per-firmware build confirmed to load on its firmware (L1–L4),
      Momentum included.
- [ ] Full edition: Range Scanner, favorites and profiles (R1–R11) and its
      memory headroom (M1–M3).
- [ ] Full edition: the beta 7 research tools and modules (B1–B9) and memory
      with modules (M4–M5).
- [ ] Catalog edition: region refusal on a device (C1–C3), then qFlipper
      screenshots and the Apps Catalog submission.
- [ ] Receive/decode, RAW capture fidelity, and authorized replay confirmed
      against real remotes.
- [ ] Regional transmit enforcement confirmed to block disallowed frequencies.
- [ ] A memory-stability pass over repeated capture/analyse/replay cycles.

This is where help is most valuable — see [Contributing](../CONTRIBUTING.md).

## Development milestones

Work proceeds one milestone at a time, each through focused pull requests with
host tests, every edition's firmware builds, and a list of the hardware checks it
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
      report export, corrupted-file handling (implemented; hardware checks
      F11–F12d pending).
- [ ] **6. Interface pass**: consistent layout, status, errors, shortcuts
      (progress percentages, messages, Database shortcuts and saved sort
      implemented; hardware checks F13–F13b pending; the radio screens' memory
      check came with milestone 8).
- [ ] **7. Optional external CC1101** through the firmware's device layer
      (implemented: selection, detection, 5 V control, status, region check;
      hardware checks F14–F14e pending).
- [ ] **8. Performance and reliability**: memory diagnostics, lifecycle tests
      (implemented: heap sampling and About figures, measured receive-session
      cost with a refusal before starting when it no longer fits, Database
      and recorder lifecycle tests; hardware checks F15–F15c pending).
- [ ] **9. Testing per feature**: unit, real-format, malformed-input and
      low-memory tests, static analysis, every firmware build in CI (implemented;
      hardware check F4f pending: fuzz targets for the RAW reader, analyzer, Pulse Timeline, Database
      index and Signal Info parser, with their corpus replayed by every test
      run; GCC `-fanalyzer` and clang-tidy on the device code; format tests
      that build the firmware's own FlipperFormat code and load its 85
      Sub-GHz test files, settings and saving included).
- [x] **10. Release management**: correct builds, checksums and provenance
      for every beta (release metadata check in CI, provenance verified before
      upload, weekly firmware watch with canary builds; `1.0.0-beta.3`
      published with milestones 1–9).

## After beta 3

- [x] **Decode with Firmware**: the firmware's own decoders run over a saved
      RAW capture, listing each decode once with its count and times
      (published in `1.0.0-beta.4`; hardware check F4g pending).
- [x] **Decoder tests**: the firmware's own decoders built on the host and
      run over its RAW test captures in CI. They found the Official-only
      Receive crash fixed in `1.0.0-beta.4` (hardware check F5a pending).
- [x] **Analyzer scored on real captures**: Unknown Protocol Analysis run
      on the firmware's 50 paired test captures in CI and scored against
      the decoders' timing; glitch, pairing, preamble and separator rules
      that came out of it (published in `1.0.0-beta.5`).
- [x] **Saved keys and custom presets**: keys saved from Receive and the
      Hopper name their real preset, so the stock app opens them; Replay
      checks a file's custom preset before the radio loads it (published in
      `1.0.0-beta.5`; hardware checks F6g and F7 pending).

## Beta 6: editions

- [x] **Two editions, one source tree**: Full (RogueMaster, Momentum,
      Unleashed) and Catalog (Official), as feature switches in
      `radiogeddon_edition.h`; shared data folder.
- [x] **Momentum build** against its own pinned SDK, in CI and releases.
- [x] **Range Scanner, scan profiles, favorites, fine stepping, Radio bands**
      (Full; hardware checks R1–R10 pending).
- [x] **Catalog transmit check** from the firmware's region, explained on
      screen (hardware checks C1–C3 pending).
- [x] **Checksum structure hypotheses** (Full; hardware check R11 pending).
- [x] **Engine lifecycle tests** and imported-symbol verification for every
      build.
- [ ] **Apps Catalog submission**: manifest, description, changelog,
      validation with the catalog's own bundler are ready; blocked on
      qFlipper screenshots from a real device.

## Beta 7: research tools (Full edition)

- [x] **Waterfall**: RSSI sweep history of the Range Scanner, missing
      measurements shown as such, CSV export (hardware checks B1–B3 pending).
- [x] **Bitstream Explorer**: frames, bits, bytes from any offset, diff and
      fields of a RAW capture (hardware check B4 pending).
- [x] **Multi-Capture Compare** of up to 8 recordings, one at a time
      (hardware check B5 pending).
- [x] **Research Sessions** with safe saves and grouping suggestions
      (hardware checks B6–B8 pending).
- [x] **Modules**: the optional tools load only while used (hardware check
      M4 pending).
- [x] **Grouped Full main menu**; the Catalog edition's is unchanged.
- [x] **Analyzer**: pulse/gap pairing, every encoding's fit shown, repeat
      timing (hardware check B9 pending).
- [x] **Offline UI tests** on a host canvas with the firmware's fonts.

## Toward a stable 1.0.0

- [ ] Fix whatever hardware testing surfaces.
- [x] Persist Settings (frequency, modulation, scanner options) across launches.
- [x] Save analysis reports to the SD card as text.
- [x] Rename recordings from the Database screen.

## Later

- [x] Custom frequency entry (published in `1.0.0-beta.5`; hardware check
      F1f pending).
- [ ] A custom modulation/preset editor.
- [x] CRC/checksum structure guesses (Full edition, beta 6).
- [x] Bit-field views across several captures (Bitstream Explorer and
      Multi-Capture Compare, beta 7).
- [ ] Richer analysis: more encodings.
- [x] A waterfall of past sweeps (beta 7).
- [ ] Range Scanner zoom.
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
