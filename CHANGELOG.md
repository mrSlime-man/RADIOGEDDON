# Changelog

All notable changes to RadioGeddon are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/) (pre-1.0.0 releases use
`-beta.N` suffixes).

## [Unreleased]

### Added
- **Scanner upgrade** (narrowband RSSI scanner): per-frequency noise-floor
  estimate and overall floor, activity detection with an adjustable threshold
  and hysteresis, peak hold, burst counters, configurable scan list (bands or
  custom), configurable dwell, optional hold on the first active frequency,
  pause/resume, and CSV export of results to `apps_data/radiogeddon/scans/`.
  The sweep now runs on its own thread.
- **Frequency Hopper 2.0**: configurable hop list, dwell and activity hold;
  per-frequency noise floor with floor-relative detection (shared Threshold
  setting); decodes also hold the hopper; lock/unlock and step-to-next
  buttons; statistics screen (hold OK) with per-frequency counts and the last
  16 activity periods; optional automatic RAW recording of each activity
  period, saved without overwriting existing files. Runs on its own thread
  and samples RSSI every 10 ms.
- **Settings are saved** to `apps_data/radiogeddon/settings.txt` and restored
  at launch (frequency, modulation and scanner options).
- **Signal Analyzer 2.0** (*Unknown Protocol Analysis*): analyses the whole RAW
  file instead of its first 4,096 samples, streaming it through about 8 KB of
  RAM. Timing peaks come from log-spaced histograms, which keeps receiver noise
  out of them; new noise share, jitter and quality grade. The encoding is now
  chosen by trial-decoding every frame as PWM, PPM and Manchester (Manchester
  and PPM bits are now extracted too), with the runner-up shown. Frames are
  grouped into repeated patterns (binary and hex), cut-off frames are aligned
  to their pattern, and a per-frame list shows each frame's time, length and
  pattern. Bit-length estimate and an ID candidate from the longest constant
  run.
- **`[OBSERVED]` label** for direct measurements, separate from
  `[HYPOTHESIS]` inferences and `[CONFIRMED]` firmware decodes.
- **RAW comparison** adds a frame-pattern comparison that does not depend on
  when each recording started.
- **Pulse Timeline** (Database → RAW file → *Pulse Timeline*): the recording
  as a zoomable, scrollable waveform with frame-start markers, pulse
  durations, an overview bar and frame-by-frame navigation (OK / hold OK).
  Only a 1,024-sample window is held in RAM; the rest streams from the SD card
  through seek checkpoints.
- **Streaming RAW recording**: captures are written to the SD card while
  recording, so their length is limited by the card, not by RAM (previously
  16,384 samples). The radio thread only appends to a lock-free buffer of 4 to
  32 KB (sized to the free heap) and never waits for the card; a separate
  writer thread empties it. If the card falls behind for longer than the
  buffer lasts, samples are dropped and counted: the REC line shows `lost N`
  (or the buffer fill once it is half full), the saved file ends with a
  `# Lost:` comment, and Unknown Protocol Analysis reports the count. The REC
  line also shows the recording time. A failed SD write stops the recording
  with `SD card write failed`.
- **Save result screen**: after saving, a popup shows the file name and, for
  RAW captures, the sample count, duration and any lost samples.
- **Signal Database list** replaces the file browser: every `.sub` with its
  type (protocol, `RAW`, other kind or `BAD` for damaged files), frequency and
  date; sort by date, name, frequency or protocol (Left cycles); Options
  (Right) to filter by RAW, decoded, one protocol, duplicates or damaged
  files, search names, and reload. Duplicates are marked with `=` (decoded:
  same protocol, frequency, bits and key; RAW: identical contents). The index
  is sized to the folder and the free heap, exists only while the Database is
  open, and is re-read after a rename or delete.
- **File details** for a Database file: size, date, type, frequency, preset,
  samples or bits, and the names of its duplicates.
- **Rename** from the Database. Names the SD card cannot store and names
  already used are refused, so a rename never replaces another file.
- **Save report to SD**: writes a file's analysis reports, with their labels
  and a legend, to `apps_data/radiogeddon/reports/<name>.txt`, never
  replacing an existing report.

### Changed
- A damaged `.sub` in the Database now opens with File details, Rename and
  Delete instead of only an error tone.
- The file menu keeps its highlighted item when you come back from a report.
- Saving never overwrites an existing file: a name that is taken gets `_2`,
  `_3` and so on (the result screen shows the final name).
- Back while recording now stops the recording and opens the name screen;
  Back there discards it. Previously leaving discarded it silently.
- If recording cannot start, the hint line says why (`REC: Not enough
  memory` or `REC: Cannot create file`) instead of only blinking the LED.
- RAW timing similarity now streams both files in full instead of loading the
  first 4,096 samples of each (32 KB of RAM), so it uses a few hundred bytes.
- Analysis shows `Analyzing...` / `Comparing...` while it reads the file, and
  reports *Not enough free memory* instead of starting when the heap is short.
- Encoding names in reports are now `PWM`, `PPM` and `Manchester`.

## [1.0.0-beta.2] - 2026-10-09

Corrected beta. `1.0.0-beta.1` crashes on launch on RogueMaster with "Out of
memory"; this release fixes that and is the recommended download.

### Fixed
- **Out-of-memory crash on launch** (seen on RogueMaster): the app no longer
  allocates the 64 KB RAW capture buffer, the Sub-GHz protocol environment,
  the manufacturer keystore or the per-protocol decoders at startup. The
  decoders and keystore are now created when Receive/Hopper starts and freed
  when it stops; the RAW buffer is created when recording starts, sized to the
  free heap (up to 16384 samples), and freed once the capture is saved. If too
  little memory is free to record, the LED blinks red instead of crashing.

## [1.0.0-beta.1] - 2026-10-09

> **Known crash:** on RogueMaster this build crashes on launch with "Out of
> memory" before the main menu. Use `1.0.0-beta.2` or later.

First public beta: the two development lines are unified into one standalone
Sub-GHz analysis toolkit, published with multi-firmware builds, documentation
and a reproducible release pipeline. **On-device behaviour is not yet verified
on physical hardware** — see `docs/VERIFICATION.md`.

### Added
- **Modules:** Sub-GHz Scanner, Frequency Hopper, RAW Signal Capture, Protocol
  Identification (firmware decoders), Signal Analyzer, Unknown Protocol Analysis
  (PWM/PPM/Manchester hypotheses, framing, bit extraction), Signal Comparison
  with a RAW timing-similarity score, Device ID Candidate Detection, Rolling
  Code Classification, Cryptographic Structure Heuristics, Signal Database, and
  Authorized Signal Replay. See `docs/FEATURES.md`.
- `[CONFIRMED]` / `[HEURISTIC]` / `[HYPOTHESIS]` labelling across all analysis
  output; no key recovery, decryption or rolling-code prediction.
- **Three firmware builds** from one source tree — Official (API 87.1),
  Unleashed (88.9) and RogueMaster (88.16) — each against its own, SHA-256
  pinned SDK; API and `.fap` manifest verified for every artifact.
- **CI/release pipeline:** a shared build workflow (links, 55 host checks under
  ASan/UBSan, three firmware builds, lint) runs on every PR and release tag;
  tag releases rebuild from the tagged commit, verify uploaded assets against
  `SHA256SUMS`, attach build-provenance attestations, and publish.
- Branding (logo, README banner, social preview), full `docs/` set, community
  health files (Contributing, Code of Conduct, Security, issue/PR templates)
  and host-testable analysis engine (`helpers/rg_analyzer.*`).

### Fixed (relative to the pre-merge branches)
- **Critical:** device presence is no longer gated on `subghz_devices_begin()`'s
  return value (which is `false` for the internal CC1101), which would have
  reported the built-in radio as absent on every real device.
- Replay reproduces the capture's exact modulation (including a file's custom
  CC1101 register set) and refuses unknown presets instead of transmitting on
  the wrong one.
- Deleting a recording now requires confirmation.
- Retained analyzer-branch stabilization: firmware-matching RX start/stop order,
  the TX region gate, replay not mutating the stored file, cross-thread GUI
  safety, and invalid-frequency guards.

### Known limitations
- No on-device hardware verification yet (`docs/HARDWARE_CHECKLIST.md`).
- Settings are not persisted across launches; internal radio only; RAW capture
  capped at 16,384 samples (engine analysis uses the first 4,096).

[Unreleased]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.2...HEAD
[1.0.0-beta.2]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.1...v1.0.0-beta.2
[1.0.0-beta.1]: https://github.com/mrSlime-man/RADIOGEDDON/releases/tag/v1.0.0-beta.1
