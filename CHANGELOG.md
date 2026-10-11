# Changelog

All notable changes to RadioGeddon are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/) (pre-1.0.0 releases use
`-beta.N` suffixes).

## [Unreleased]

## [1.0.0-beta.7] - 2026-10-11

Research tools for the Full edition (waterfall, Bitstream Explorer,
multi-capture comparison, research sessions), loaded as modules only while in
use; a grouped Full main menu; a better Unknown Protocol Analysis; offline UI
tests. Not yet verified on hardware.

### Added
- **Waterfall** (Full): an RSSI sweep history of the Range Scanner — each
  complete sweep one row, newest on top, intensity as ordered dither over the
  noise floor with readings over the threshold solid, unmeasured cells drawn
  as their own pattern and never filled in (`rg_waterfall`). Cursor, peak,
  scroll back, pause, sensitivity, floor compensation, receive at the
  cursor's strongest point and CSV export of the history. Labelled `RSSI
  sweep`: sequential narrowband readings, not a wideband capture. History
  buffer at most 8 KB, sized from the free heap and refused before anything
  is allocated.
- **Bitstream Explorer** (Full): a RAW capture's inferred frames as a frame
  list, bits, bytes from any bit offset (leftover bits shown apart, never
  padded), a diff against every comparable frame (`.` same, `X` changes, `?`
  too few frames) and a field view with binary, hex and decimal (`rg_bits`).
  Reuses the analyzer; no bit is inferred beyond its result.
- **Multi-Capture Compare** (Full): 2 to 8 recordings analysed one at a time
  into ~200-byte summaries; a report labelled `[OBSERVED]`, `[HEURISTIC]` and
  `[HYPOTHESIS]` with frequency, preset, Te, encoding and length consistency,
  identical frames, constant and changing bits, runs classified as constant,
  button-like, counter-like or no simple rule, and a noise check
  (`rg_multi`). A constant field is never called a verified serial.
- **Research Sessions** (Full): named groups of recordings in
  `apps_data/radiogeddon/sessions/` (`rg_session`). Add, remove, open,
  compare and export; an active session collects new captures; renaming a
  recording in the app updates the sessions naming it. Saves are written
  beside the old file, read back and swapped in, with a backup restored after
  an interrupted save. *Suggest groups* proposes recordings with the same
  frequency, protocol and frame length saved close together; nothing is
  grouped without confirmation, and recordings are never deleted or moved.
- **Modules** (Full): the Waterfall, Bitstream Explorer, comparison, Unknown
  Protocol Analysis and Sessions code are plugins embedded in the `.fap`,
  loaded when their screen opens and unloaded when it closes. A module is
  loaded only when its code and work fit the free heap (`rg_elf` reads its
  size); otherwise `Not enough memory`.
- **Full main menu** grouped into Scan, Receive & Record, Analyze, Database,
  Sessions, Settings and About. The Catalog edition's menu is unchanged.
- **Unknown Protocol Analysis** lists every encoding's fit, explains a
  pulse/gap pairing decision, names the frames a checksum hypothesis was
  tested on, and reports how often the main patterns repeat within a press.
- **Offline UI tests** (`make -C test ui`, in CI): screens drawn on a host
  canvas with the firmware's own fonts, checked for text off screen and
  overlaps, keys driven through each screen; synthetic previews uploaded as a
  CI artifact, marked as not hardware screenshots.
- **Tests**: waterfall, bits, multi, session and ELF-size unit tests; the
  waterfall engine in the lifecycle tests; a session-file fuzz target
  (`fuzz_session`) and the module size estimate fuzzed. Host checks: 1,962
  (was 1,548).
- `scripts/fap_size.py` (resident size of a `.fap` or module) and
  `scripts/verify_fap.py --modules` (unpacks and checks the embedded
  modules); every Full build verifies its five modules.

### Changed
- **Unknown Protocol Analysis**: when Manchester's grammar fits but every
  pulse pairs with its gap by one rule (always opposite, or always equal),
  the pulse-width reading is taken; its confidence is then capped at 70.
  Encoding family right on 43 of the firmware's 50 RAW test captures (was
  41), frame length on 35 (was 33), Te still 50; no capture got worse. The
  capture test's floors rise to match.
- **Receive and Hopper memory check**: before the first measurement, a
  session is refused only when less than 16 KB is free.
- Footers sit one pixel higher so descenders are not cut off.

### Fixed
- The Range Scanner's setup and scan screens no longer loop back into a
  failing start when the scan cannot start; they return with the message.
- Two uninitialised-value paths found by static analysis in new code, and a
  session name with a trailing space (found by fuzzing) that did not survive
  a save and load.

### Measured
- Resident code (allocated ELF sections, from the built files): Catalog
  77,173 B (beta 6: 75,704 B), Full 104,388 B (beta 6: 94,676 B). Built
  into the app, the new tools made the Full edition 128,309 B; as modules
  they leave it 9,712 B larger than beta 6 (most of it analyzer work shared
  with the Catalog edition), and each module takes 2,740 to 16,383 B only
  while in use.

## [1.0.0-beta.6] - 2026-10-10

Two editions from one source tree, a Momentum build, a range scanner,
favorites and scan profiles, the Catalog edition's region check before
transmitting, and Apps Catalog preparation. Not yet verified on hardware.

### Added
- **Editions.** `radiogeddon_edition.h` defines the Full and Catalog editions
  as feature switches. `application.fam` builds the Catalog edition (appid
  `radiogeddon`), so a plain `ufbt` — the Apps Catalog's build — produces it;
  `scripts/stage_edition.py full` writes a copy whose manifest builds the Full
  edition (appid `radiogeddon_full`, "RadioGeddon Full"). A build that defines
  neither edition is a Catalog build. Both editions keep their data in
  `/ext/apps_data/radiogeddon`.
- **Momentum build** (`radiogeddon-full-momentum.fap`), compiled against
  Momentum mntm-012's own SDK (SHA-256 pinned, the file Momentum's update index
  lists), in CI and in releases.
- **Range Scanner** (Full): start / end / step sweep of up to 256 points over
  every band the radio in use accepts, gaps skipped (`rg_range`); dwell,
  threshold, pause on hit, recalibration, peak hold, counters, sweep-time
  estimate, CSV export, a spectrum screen (`rg_spectrum`, median noise floor
  over all points), and long OK to receive and record at the cursor. The
  engine and screen exist only while the scan is open, and a scan that would
  not fit in the free heap is refused before anything is allocated.
- **Scan profiles** (Full): save, load and delete named range setups in
  `apps_data/radiogeddon/profiles/`; replacing or deleting one asks first.
- **Favorites** (Full): up to 24 frequencies in
  `apps_data/radiogeddon/favorites.txt`; receive or record on one, make it the
  receive frequency, or scan / hop the whole list (Settings → Scan source,
  Hop source).
- **Fine frequency stepping** (Full): Settings → Freq step makes Left/Right on
  `Frequency MHz` step 1 kHz to 1 MHz within the radio's bands, jumping gaps.
- **Radio bands** screen (Full): the receive bands the radio in use accepts,
  measured from its driver (`rg_range_probe_bands`), and the firmware region's
  transmit bands.
- **Checksum structure hypotheses** (Full) in Unknown Protocol Analysis
  (`rg_checksum`): XOR, sum (plain, inverted, negated) of 8- or 4-bit words,
  CRC-8 for six polynomials and two initial values, and parity, over the
  distinct frames of the modal length, reported only when they fit every
  frame and there are enough distinct frames.
- **Long OK on the Scanner** opens Receive and starts a RAW recording.
- **About** shows the edition and the firmware's region.
- **Tests**: edition switches built as each edition and with none
  (`test_edition`), the transmit check in both editions (`test_txpolicy`),
  bands and range plans against each firmware's real tuning ranges
  (`test_range`), spectrum maths (`test_spectrum`), checksum hypotheses
  (`test_checksum`), settings shared between editions and older releases,
  favorites and profiles (format tests), and engine lifecycle tests
  (`make -C test lifecycle`: the real Scanner, Range Scanner and Hopper
  started and stopped repeatedly on real threads against a fake radio, every
  allocation counted). Host checks: 1,548 (was 665).
- **Apps Catalog preparation**: `catalog/` (description, changelog, manifest
  template, screenshot instructions), `scripts/check_catalog.py` (the
  catalog's rules, with its own Markdown filter) and
  `scripts/catalog_bundle.py` (the catalog's own bundler, step by step), both
  in CI.
- `scripts/verify_fap.py --symbols`: every symbol a `.fap` imports must be
  exported by its SDK's `api_symbols.csv`; every build checks it.

### Changed
- **Transmit check.** The Catalog edition refuses a transmission unless the
  radio accepts the frequency, `furi_hal_subghz_is_frequency_valid()` accepts
  it, the firmware has a provisioned region and that region allows the
  frequency (`rg_txpolicy`), and explains a refusal on screen; Replay says
  beforehand when it will refuse. The Full edition relies on the firmware's own
  transmit authorization (`subghz_devices_set_tx`, as before in both) and no
  longer adds its own region check for the external module. A refusal by the
  firmware's driver now reads `Firmware blocked TX`.
- **Release artifacts** are named by edition and firmware:
  `radiogeddon-catalog-official.fap`, `radiogeddon-full-roguemaster.fap`,
  `radiogeddon-full-momentum.fap`, `radiogeddon-full-unleashed.fap`. The
  release workflow requires all four.
- **Unknown Protocol Analysis**: Te right on 50 of the firmware's 50 RAW test
  captures (was 48), frame length on 33 (was 29). The glitch floor is 140 µs
  (was 100; the shortest protocol Te in the firmware is 160 µs and noisy
  captures show glitch peaks at 132 µs). A frame that fills the bit buffer or
  is cut off, and whose bits repeat themselves (at least 97 % at one period,
  with a repeated unit that carries information), is cut to one repeat and
  marked `rep`; frames that end on their own are never cut.
- Static analysis runs on each edition as it is built (`--root`); the firmware
  watch also follows Momentum and canary-builds the right edition per firmware.

### Measured
- Resident code (text + rodata + data + bss, from the built files): Catalog
  75.6 KB, Full 94.5 KB, beta 5 73.8 KB.

## [1.0.0-beta.5] - 2026-10-10

Saved keys open in the stock Sub-GHz app again, Replay checks custom presets,
Unknown Protocol Analysis reads real captures better, and Settings takes a
typed frequency. Not yet verified on hardware.

### Changed
- **Unknown Protocol Analysis reads real captures better.** Scored on the
  firmware's 50 paired RAW test captures against each protocol's decoder,
  its Te is now right for 48 (was 39), its encoding family for 41 (was 33)
  and its frame length for 29 (was 16). Receiver glitches below 100 µs are
  no longer taken for Te or a bit width; PWM and PPM are also tried paired
  gap-first, as CAME sends them; a square-wave preamble no longer votes for
  Manchester; longer frames weigh more in the vote; repeats sent closer than
  7 Te apart are split at their rarer, longer gap; and a carrier pulse of
  14 Te or more ends a frame. Results stay `[HYPOTHESIS]`.
- **Frequencies are shown with their kHz.** 433.075, 303.875 and 434.775 MHz
  read as 433.075, 303.875 and 434.775 in Settings, Receive, Replay, the
  Database list and the Hopper statistics, where they were cut to 433.07,
  303.87 and 434.77.

### Added
- **Custom frequency**: press OK on `Frequency MHz` in Settings to type any
  frequency in kHz for *Receive & Record*. The radio in use must be able to
  tune it (otherwise `Cannot tune there` and nothing changes); it is saved
  with the settings. Transmitting is still checked against the firmware's
  region rules. The keyboard exists only while it is open.
- **Capture tests** (`make -C test captures`, run in CI): the analyzer on
  the firmware's own RAW test captures, scored against each protocol's
  timing in the firmware's decoder source. A drop in any score fails the
  test.

### Fixed
- **Saved keys open in the stock Sub-GHz app and in Replay.** Every key
  saved from *Receive & Record* or the Hopper since `1.0.0-beta.1` was
  written as `Preset: FuriHalSubGhzPresetCustom` with an empty
  `Custom_preset_data`, because the decoders were given the preset's file
  name where the firmware expects its short name (`AM650`). The stock app
  refuses such a file (`Cannot parse file`, in the Official, Unleashed and
  RogueMaster sources) and Replay said `Unsupported file`. Keys are now saved
  with their real preset; the decoder tests save every decode on each preset
  with the firmware's own code and check it. RAW recordings were never
  affected. Keys saved earlier show `Bad custom preset` in Replay, and
  [Troubleshooting](docs/TROUBLESHOOTING.md#a-saved-key-wont-open) shows the
  one-line fix.
- **Replay checks a file's custom preset before loading it.** A `.sub` file
  can carry its own CC1101 register list (`Custom_preset_data`). The
  firmware writes that list to the radio without bounds or checks, so a
  damaged one made it read past the buffer, and an address above the
  configuration registers would be run as a command: 0x35 (STX) would
  start transmitting before any region check. Replay now refuses such a file
  with `Bad custom preset` and writes nothing to the radio. Every preset the
  firmware ships passes the check (`make -C test decoders`).

## [1.0.0-beta.4] - 2026-10-10

Fixes a crash on Official firmware and adds Decode with Firmware. Not yet
verified on hardware.

### Fixed
- **Receive and the Hopper could crash on Official firmware when they decoded
  a CAME Atomo or Alutech AT-4N remote.** Those two firmware decoders read
  the file name of a rainbow table, without checking it, when they describe a
  decode. RadioGeddon gives them none, since it does not undo rolling-code
  obfuscation, so the name was NULL. Their decodes are now described by name,
  bit count and key, with a note that serial, button and counter need a
  table. Found by the new decoder tests; not yet reproduced on hardware.
  Unleashed and RogueMaster check for a missing table and were not affected.

### Added
- **Decode with Firmware**: a new item in a RAW capture's Database menu runs
  the firmware's own protocol decoders (the ones Receive uses, with the same
  keystore) over the saved recording, feeding them every sample the way the
  firmware's `subghz decode_raw` command feeds a file. Each distinct decode is
  listed once as `[CONFIRMED]` with how many times and when it was decoded and
  the description Receive shows; a capture nothing decodes says so. Shows a
  progress percentage; the radio is not used. It checks memory like Receive
  and refuses with `Not enough memory` (`Decoders need ~N KB`) when the
  decoders no longer fit. *Save report to SD* includes it for RAW captures.
- **Decoder tests** (`make -C test decoders`, run in CI): the firmware's own
  Sub-GHz receiver and all of its protocol decoders, built on the host from
  the pinned Official release, decode the firmware's 50 RAW test captures fed
  through RadioGeddon's new RAW feeder (`rg_decode`), each to the protocol the
  firmware's own test expects. The feeder has its own unit tests (68 checks).

## [1.0.0-beta.3] - 2026-10-10

Milestones 1 to 9: scanner, hopper, analyzer, streaming recording, database,
interface, external radio, memory and testing. Not yet verified on hardware.

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
- **External CC1101 module** (Settings → `Radio`): uses the firmware's
  `cc1101_ext` driver only when a module answers, with optional 5 V on GPIO
  pin 1 (`Ext radio 5V`), an `EXT` marker on the radio screens, the radio
  named in About, and the choice saved. Transmitting through it is also
  checked against the region table and re-probes the module first.
- **Memory diagnostics** in About: free heap now, largest free block, free
  heap at app start, the lowest the app saw (and the step before it), the
  app's peak use, the lowest since boot, the total heap, and what a receive
  session took when last measured. The app samples the heap on every screen
  tick and after its large allocations, and writes a summary to the log when
  it closes.
- **Fuzz tests and static analysis** (development): libFuzzer targets for
  the RAW reader with the analyzer, the analyzer and Pulse Timeline on raw
  timing values, and the Database index with the Signal Info line parser.
  Their corpus is replayed by every `make -C test check`, and CI fuzzes each
  target for 60 s. CI also runs GCC's `-fanalyzer` and clang-tidy over the
  device code with the exact build flags.
- **Format tests** (development): the firmware's own FlipperFormat and stream
  code, built on the host with RadioGeddon's settings and `.sub` loading
  code, run against the 85 Sub-GHz test files of the firmware's unit tests
  (downloaded at the pinned release and checksum-checked, not committed):
  every file loads with the details an independent reading finds, the
  Database index and RAW reader agree, settings survive a save and reload,
  malformed settings fall back field by field, and a save that fails
  part-way keeps the old file.
- **Release checks** (development): `scripts/release_meta.py` checks that the
  version, release notes, CHANGELOG section, the API listed for each `.fap`
  and the download links agree. CI runs it on every pull request; the Release
  workflow refuses a tag that fails it.
- **Firmware watch** (development): a weekly workflow lists newer Official,
  Unleashed and RogueMaster versions with their API versions and builds the
  app against every newer Official or Unleashed SDK. Its first run found that
  Official 1.5.1-rc moves to API 88, where the current Official build will
  not load; the app builds against it.

### Changed
- **The Release workflow verifies build provenance** (`gh attestation
  verify`) for every `.fap` before anything is uploaded, the same check the
  installation guide gives users.
- **Progress percentages** for long SD-card work: reading the Database
  (`Reading files...`), Unknown Protocol Analysis, Pulse Timeline indexing,
  RAW comparison and report writing. Opening a large RAW capture shows
  `Opening...`.
- The Database remembers its sort order across launches (saved with the
  settings), and **holding OK** on a file opens its details directly.
- Picking an unreadable file in `Compare with...` shows `Cannot compare`
  instead of only an error tone.
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
- **Receive and Hopper check memory before starting the radio.** Each receive
  session measures what its decoders, keystore and worker took, and the app
  keeps that figure (per firmware) with the settings. When less than that
  plus 6 KB is free, the screen shows `Not enough memory` with both figures
  instead of starting and risking an out-of-memory crash. Until a session has
  been measured on the running firmware, nothing is refused.
- Saving a decoded signal that fails part-way now removes the partial file
  instead of leaving a damaged `.sub` in the Database; a failed Scanner CSV
  export no longer leaves a cut-off file either.
- Pressing OK to save a decode while recording now stops the recording and
  asks for its name first (the decoded list is kept). Before, the RAW capture
  was silently deleted when the receiver restarted.
- Settings are written to a new file and swapped in only when complete, so a
  card error while saving keeps the previous settings instead of a cut-off
  file.
- A temporary recording left on the card by a reboot or flat battery during
  recording is removed when the app starts.
- Signal Info no longer misreads a RAW value beyond ±2,147,483,647 (only a
  damaged or hand-edited file has one): it counts as the longest duration,
  as in the analyzer, instead of overflowing. Found by the fuzz tests.
- RAW files with a comma after each value (`RAW_Data: 1718, -32700, ...`,
  which the firmware's RAW player accepts and one of its own test files
  uses) are now read in full. Before, Unknown Protocol Analysis, Pulse
  Timeline and Compare found no samples in them and Signal Info counted one
  value a line. Found by the format tests.

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
