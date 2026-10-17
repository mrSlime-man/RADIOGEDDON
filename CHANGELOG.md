# Changelog

All notable changes to RadioGeddon are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/) (pre-1.0.0 releases use
`-beta.N` suffixes).

## [Unreleased]

Beta 12 stability pass (to be released together with the beta 12 features).
**Not hardware tested**; the device checks are X1–X10 of the
[hardware checklist](docs/HARDWARE_CHECKLIST.md#crash-and-relaunch-fixes-beta-12),
the analysis is in [STABILITY_BETA12.md](docs/STABILITY_BETA12.md).

### Fixed
- **Adaptive Scanner → Start scanner stopped the device (`furi_check
  failed`)**, reported on RogueMaster. Its setup screen cleared the list's
  enter callback with NULL when closing, which every firmware forbids
  (`furi_check(callback)` in `variable_item_list_set_enter_callback`). The
  same bug was in the RF Activity Logger and Database options (Catalog
  edition too). The shared list now always holds the app's own resident
  do-nothing callback, so no screen can inherit a callback from an unloaded
  module either. Module API 6.
- **Reopening the app right after closing it could fail for lack of RAM.**
  The firmware loads each code section into a heap block of its own, and the
  app's code was one 70.9 KB section. The screens' code is now a second
  section (`.rgtext`): the largest block a launch needs is 39.8 KB instead of
  70.9 KB (Unleashed Full), and every module splits the same way.
- The Hopper's engine no longer keeps a pointer to its unloaded screen's code.

### Added
- The Scanner checks memory before starting (firmware `malloc` cannot fail
  softly) and says `Not enough memory` instead of stopping the device.
- `RG.mem` log lines at launch, start, module load/unload, scanner start/stop
  and exit (free heap, largest free block, lowest free heap), and a heap
  balance at exit.
- `scripts/check_fw_contracts.py` (in CI): every call checked against the
  arguments the firmware `furi_check()`s, derived from the five pinned
  firmware trees; it finds the three beta 11 calls above.
- `RM_REF`/`RM_API` for `scripts/build_target.sh full-roguemaster`: build in
  the tree of the RogueMaster commit a device runs (API asserted).
- Tests: the Adaptive Scanner driven 1,200 times as its screens do (all
  modes, pause, reset, free/realloc; ASan and TSan); a replay of the app's
  launch on fragmented heaps (`make -C test benchlaunch`).

## [1.0.0-beta.11] - 2026-10-17

Memory and performance release: the same features using less heap while
they run, fewer SD-card operations and no 64-bit division helper. Nothing
new transmits; no feature removed. Figures are from the built files, the
device compiler's sizes and a host model of the firmware's heap — **not
measured on a device; not tested on hardware** (checklist V1–V8).

### Changed
- **Bitstream Explorer holds 4,072 B instead of 8,672 B** while open: the
  analyzer's result and the passes' working state are separate objects; the
  working state (4,600 B) is freed when the passes end, and the result is
  allocated before the file and the working state, so freeing those leaves
  no hole under it. The peak is unchanged (+8 B for a second heap header).
  Pulse Studio, the Pulse Timeline, the reports, Multi-Capture Compare and
  Find Similar keep only the result too.
- **Find Similar**: the index update lists the folder once, matches
  recordings to old records through a sorted table of 16-bit name hashes
  and compares every candidate's name exactly, so no two recordings can be
  mistaken for one (beta 10 trusted 64-bit hashes); 4 bytes per recording
  instead of 8; sizes from the listing, so half the path lookups; new
  recordings gathered by folder position, a third as many listings when
  indexing; the fingerprint builder reads along with the analyzer's second
  pass and the header is parsed without a pass over the samples (a RAW
  recording is read 4 times instead of 6). Host benchmark at 2,000
  recordings: cold peak 31.2 → 15.9 KB, warm peak 16.7 → 9.6 KB, path
  lookups 4,000 → 2,000. Rankings unchanged.
- **Database**: entries are 28 bytes instead of 40 (protocol names kept once
  each in a shared table); the whole index is one heap block (was four); the
  500-file cap is now the 32 KiB beta 10's 500-file index took, which holds
  about 600–660 files with typical names (at most 2,000). Sorting,
  filtering, search and duplicates give the same results (benchmark
  checksums equal to beta 10's).
- **Module loading**: the free block asked for before loading a module is
  computed from the module's own section table with a model of the
  firmware's loader (its relocation tables counted, and not added to the
  tool's work, since they are freed before it starts) instead of a flat
  6 KB; 1.9–3.8 KB less for 15 of the 16 modules, the 4 KB margin
  unchanged. On 4,000 simulated fragmented heaps per module, 3,952 fewer
  loads that fit are refused and none that is allowed runs out of memory.
- **No 64-bit division helper**: every 64-bit division in the app and its
  modules is done exactly by `rg_div_u64`, so `__udivmoddi4` (698 B) is
  linked nowhere (beta 10: the resident app and 8 modules); 14 of the 16
  modules are smaller. The fingerprint builder's per-sample bucket test no
  longer divides.
- Resident code: Full 84,433 B (beta 10: 84,313 B), Catalog 86,565 B
  (86,513 B).

### Fixed
- Beta 10's module check reserved less than the Range Scanner module's
  relocation tables and file object take while it loads (absorbed by the
  margin until now).

### Testing
- A host model of the firmware's heap_4 allocator (`test/stubs/heap4.h`),
  with AddressSanitizer poisoning inside it; `make -C test memstress` (CI):
  the heavy operations with every allocation in the model, a low-memory
  sweep where no allocation may fail, a long random session with no leak
  or lasting fragmentation; `make -C test benchmem` for the tables
  ([Performance](docs/PERFORMANCE.md)).
- `test/bench_modules.c` (CI): each built module's real load sequence on
  fragmented heaps, and 20,000 load/unload cycles.
- `scripts/module_cost.py --check` (CI): the C load-cost model against an
  independent reading of the built modules.
- New checks: the heap model (128), 64-bit division (6.2 million), Find
  Similar with names sharing a 16-bit hash and a duplicated record, the
  builder along the second pass, the Database with 24 distinct protocols.

### Investigated, unchanged
- **Activity Heatmap grid**: every field of its 6-byte cell is needed at full
  precision when columns are merged; a smaller cell would drop the peak dBm
  or the margin's precision.
- **A shared work arena**: the heavy operations never overlap and free in
  reverse order of allocation; the heap model shows the free space whole
  again after each, so an arena would add an allocator without saving
  memory.

## [1.0.0-beta.10] - 2026-10-17

Optimization release: the Full edition's resident code drops from 119,132 B
to 84,313 B (−29 %), so it starts again on devices where beta 9 ran out of
memory; files survive a reboot; the Database and Find Similar do far less
work; and a few new on-device tools. Nothing new transmits. **Not tested on
hardware** (checklist U1–U12).

### Added
- **Recording recovery** (both editions): the RAW writer syncs at most every
  2 s, and a capture cut off by a reboot or a flat battery is checked,
  repaired (complete values only, a `# Recovered:` note) and offered at the
  next start: Keep (`signals/Recovered_<time>.sub`), Delete, Back keeps.
  Before, the leftover file was deleted. Full edition: a module loaded only
  then.
- **RF Activity Heatmap** (Full; Scan → Activity Heatmap): a saved activity
  log as frequency × time; not observed, observed-quiet and active cells
  told apart; bursts or strength; coverage per cell.
- **Find Similar scope**: all recordings, within 1 hour, within 1 day, or
  (while a research session is active) only that session's recordings.
- **Diagnostics** (both editions; About): UI responsiveness (slowest tick
  gap, stalls over 250 ms), module loads, recordings (losses, overruns,
  write errors, SD write speed), heap fragmentation, messages; **Save
  report** writes them to `diagnostics/` (counts only, no capture data).
- **Battery saver** (Settings, both editions, off by default): live screens
  redraw at most twice a second, the logger's status every 5 s, the
  animation once a second; radio work unchanged. Battery effect not measured.

### Changed
- **Resident code, Full edition: 119,132 → 84,313 B** (Momentum build;
  measured from the built file with `scripts/fap_size.py`, not on a device).
  The Range Scanner (with the Waterfall scene and scan profiles), Settings
  (with Radio bands), the Frequency Hopper (with its statistics), the
  Bitstream Explorer (with the analyzer it runs), About (with the
  diagnostics text) and recording recovery are now modules loaded only while
  their screen is open, like the beta 9 tools: 16 modules, nothing they do
  changed. Opening one reads it from the SD card first; when it does not fit
  the free heap the screen says `Not enough memory`.
- **Bitstream Explorer peak heap −4 KB**: it keeps the analyzer for its
  result instead of copying the result out, so the 8,672 B analyzer and a
  4,072 B copy no longer exist at once (and no hole is left in the heap).
  Computed peak while it opens, code included: about 25 KB beyond the
  resident app (beta 9: about 19 KB, but on a resident app 35 KB larger).
- **Catalog edition: 80,553 → 86,513 B** resident (it has no modules):
  recording recovery (+2.4 KB), diagnostics and the battery saver
  (+2.1 KB), crash-safe saves (+0.6 KB) and the faster Database (+0.6 KB).
- Module interface version 5 (the host table gained the radio, recording,
  Range Scanner and redraw entries the new modules call).
- **Crash-safe saves** for settings, journal, favorites, scan profiles and
  the fingerprint index: a commit marker makes every interrupted save
  recoverable to the old or the new version (the firmware's rename copies;
  a reboot during it used to lose both).
- **Database**: sorting, searching and duplicate marking in O(n log n),
  identical results (7–18× less work at 500–1,000 files on the host).
- **Fingerprint index**: one folder listing when nothing is new, batches
  sized to the heap, 64-bit name hashes in a sorted array; up to 2,000
  recordings (was 500), or what the heap allows, never refused for a short
  heap. 500 recordings, first search: 43 → 9 folder listings.
- **Protocol discovery**: preamble filler between back-to-back repeats is no
  longer counted as data (Nero Sketch 89 → 39 bits); the capture tests also
  score against the lengths the decoders actually decode (43 → 44 of 50).

### Fixed
- **Full edition did not start on some devices (out of memory)** with beta 9's
  119 KB of resident code; see *Changed*.
- Data races on the Scanner, Range Scanner and Hopper stop flags (found by
  ThreadSanitizer, now a CI job).
- Two names with equal 32-bit hashes kept one recording out of the
  fingerprint index.


## [1.0.0-beta.9] - 2026-10-11

Standalone Research Lab (Full edition): signal fingerprints and Find Similar
Signals, Pulse Studio, Smart Protocol Discovery, the Adaptive Scanner, the RF
Activity Logger, the Research Journal with optional XP and achievements, the
Dolphin Assistant, dolphin customization and Room 2.0. Everything runs on the
Flipper; nothing new transmits. Not yet verified on hardware.

### Added
- **Signal fingerprints** (`rg_fp`, pure C, host-tested and fuzzed): a
  64-byte summary of a RAW recording computed in one streaming pass of the
  analyzer: frequency, modulation, Te, pulse/gap ratio histogram (12 bins of
  Te ratios), preamble cells, sync ratio, frame count and spacing, repeats,
  bit length, encoding and pattern stability. Kept in
  `apps_data/radiogeddon/fingerprints.idx` (fixed-size records with a CRC,
  written to a temporary file and renamed; a damaged index is rebuilt).
- **Find Similar Signals** (Full; Analyze → Find Similar..., or a
  recording's menu): the 8 closest recordings with a score, "Why similar?"
  (the features that agree and those that contradict), Open, Compare with
  this and Compare all matches. Scores are capped by the evidence: no frame
  structure caps at 30, RAW against decoded at 60, disagreeing timing at 65;
  bit-pattern evidence counts only between recordings of the same encoding.
- **Pulse Studio** (Full; Analyze → Pulse Studio..., or a recording's
  menu): the waveform with eight zoom levels, pan, a cursor on a pulse or gap
  (its duration in µs and Te), a measuring marker (long OK) with the
  distance to the cursor, preamble, glitch and frame-end marks and the bit
  each symbol decodes to; Frame, Bits, Hex, Diff (against the most common
  pattern) and Timing (half-Te histogram) pages. Frames are read from the
  file through the RAW reader's checkpoints one at a time.
- **Adaptive Scanner** (Full; Scan → Adaptive Scanner): Standard (the
  classic round robin), Balanced Adaptive, Activity Focused and Custom
  (the favorites in the list first). Stride scheduling weighted by a decaying
  activity score; every frequency is visited at least once per bounded
  number of picks (fairness), one frequency at most three times in a row,
  and dwell stays within bounds. The Scanner shows the mode, the selected
  frequency's share of the visits and the longest wait. Reset learning
  starts afresh.
- **RF Activity Logger** (Full; Scan → RF Activity Logger): schedule
  (Standard, Balanced or Focused), duration (15 min – 8 h or until stopped),
  interval (10 s – 5 min), part size (64 KB – 1 MB, up to 8 parts) and
  threshold. Rows per frequency and interval in `activity/activity_*.csv`
  with empty fields for "not observed" (never written as quiet), a header
  and an `# End` footer; a `_summary.txt` with the busiest frequencies,
  first and last activity and coverage. It stops when the time is up, on
  Back, when the card is full or a write fails — never overwriting.
- **Research Journal** (Full; Analyze → Research Journal, Room → Terminal):
  counters of real events (recordings, analyses, fingerprints, searches,
  comparisons, sessions, reports, activity logs, Pulse Studio, identified
  protocols, scans, Protocol Discovery), the 12 most recent events and up to
  8 identified protocol names. Saved in `journal.bin` with a CRC, written a
  few seconds after a change and never while recording; a damaged file is
  kept as `journal.bad` and the journal starts again. Clear history keeps
  counts and XP; Reset all progress clears everything. Recordings are never
  touched.
- **Research XP and achievements** (optional, Settings → Assistant tips
  and the journal): XP derived from the counters (each event type capped),
  12 achievements (First Capture, First Analysis, Fingerprinter, Comparer,
  Session Starter, Reporter, Night Watch, Explorer, Pulse Reader, Protocol
  Spotter, Lab Regular, Polymath) and levels (20·n·(n−1) XP). Nothing is
  awarded for transmitting.
- **Dolphin Assistant** (Full; Settings → Assistant tips): one rule-based
  tip at a time on the dashboard's first menu row (try Classic on low
  memory, record more samples, analyze, find similar, compare, inspect pulse
  timing, add to the session, export a report). Never while recording; at
  least 15 s between new tips and 5 min before one repeats; long OK
  dismisses it for the current recording; OK opens the suggested tool.
- **Dolphin customization** (Full; Dolphin Lab → Corner → OK, or the
  journal): headphones, goggles (as posed / always / off), antenna (classic,
  whip, dish, none), tool (wrench, probe, notebook), expression and
  accessory (bow tie, scarf, ID badge); wall decorations (plant, poster,
  lamp, mug, trophy) unlocked by research level. 14 new sprites (47 in all,
  496 bytes).
- **Animation level** (Settings → Animation): Off (a still pose per state),
  Minimal (state changes and blinks), Normal (beta 8) and Full (a swaying
  antenna, more tail and eye movement). Measured on the host over 40 s
  idle: 0, 23, 87 and 129 redraws. Off is used automatically on low memory.
- **Room 2.0**: six stations — Workbench (the recording and session you are
  working on; OK opens it), Oscilloscope, Radar (illustration), Research
  Terminal (level, XP, counts and badges from the journal), Equipment Rack
  (Scanner, Adaptive Scanner, Activity Logger, Receive; OK opens the real
  tool) and the Dolphin Corner (pose gallery, decoration).
- **New dolphin reactions**: Celebrate (an achievement, a strong match, a
  level) and Greeting (the first dashboard of a run).
- **Unified workflow**: the recording you last saved, opened or analysed is
  the *current recording*; Analyze offers it first, and the journal records
  which steps (analysed, searched, compared, studied, in a session) it went
  through, which the assistant reads.
- **Modules**: `radiogeddon_fp`, `radiogeddon_studio`, `radiogeddon_log`
  and `radiogeddon_reports` join the six of beta 8 (the lab module now also
  holds the journal, assistant and customization screens). A generic tool
  forwarder hosts module scenes in two slots and unloads a module once its
  screens are gone.
- Tests: `test_fp`, `test_adapt`, `test_actlog`, `test_journal`,
  `test_studio`, `make -C test fpindex` and `make -C test actlogfiles` (both
  in CI), the adaptive scanner in the lifecycle test, Pulse Studio, the
  adaptive Scanner, the room stations and every dolphin look in the UI test,
  and a `fuzz_fp` libFuzzer target for fingerprint index and journal files.

### Changed
- **Smart Protocol Discovery** (Unknown Protocol Analysis): a
  `Readings [HYPOTHESIS]` section lists competing readings (encoding, Te,
  frame length) with their evidence and scores; confidence drops when no
  rival reading was ruled out or noise is high. The analyzer maps each
  pulse to its bit (and marks unsure ones), keeps a populous short gap
  cluster as data (long-gap PWM), and measures PWM periods from pulse
  pairing. Benchmark on the firmware's 50 RAW test captures: encoding
  family right for 48 (was 43, 3 unidentified), frame length right for 42
  (was 35), mean confidence 87 when right and 67 when wrong (was 84).
- **Checksum hypotheses** (Full) require frames that are independent over
  GF(2): repeats of one frame no longer "confirm" an XOR or CRC.
- Settings are read and written from one table; beta 8 files load with the
  new settings' defaults (`Ui_animation`, `Assistant`, `Dolphin_look`,
  `Lab_decoration`, `Scan_mode`, `Log_interval_s`, `Log_minutes`,
  `Log_max_kb`).
- Analysis reports, crypto heuristics, comparison results and the Pulse
  Timeline moved into modules; the resident Full edition is 119,132 B (beta
  8: 119,428 B). Catalog: 80,553 B (beta 8: 77,782 B).
- The Scanner's observer reports every visit (frequency, activity, reading,
  floor and dwell) to the logger and the adaptive scheduler.

### Fixed
- The Pulse Studio bit cursor at the left edge, and labels that would have
  run off the screen (found by the new UI tests before release).

## [1.0.0-beta.8] - 2026-10-11

Dolphin Lab: an original animated dolphin mascot, a dashboard home screen,
three interface styles, cards and transitions, reactions to what the radio
really does, optional UI feedback and an interactive lab room (Full edition).
Not yet verified on hardware.

### Added
- **The RadioGeddon dolphin** (Full): an original monochrome pixel-art
  character drawn for this project, a dolphin lab engineer with headphones,
  an antenna and lab goggles. The art is text in `art/dolphin/*.txt`;
  `tools/dolphin/build_sprites.py` turns it into `helpers/rg_sprites.c`
  (373 bytes of bitmaps: body, flukes, flipper and eye poses, headphones,
  goggles, props, menu icons and the header badge), and `make -C test check`
  fails when the generated file is stale.
- **Mascot state machine** (`rg_mascot`, pure C, host-tested): Idle, Bored,
  Asleep, Saved, Analyzing, Scanning, Signal detected, Receiving, Recording,
  Low memory and Error, chosen by priority (Error > Low memory > Recording >
  Receiving > Signal detected > Scanning > Analyzing > Saved > idle states).
  Ongoing activity is a level synced from the real state on every tick (the
  receive session, the recorder, the sweep, the heap); reactions are
  deadlines, so repeated detections move a deadline and never queue. REC is
  shown only while the recorder runs. Idle animation is calm (blinks,
  breathing, a tail flick, looking around, examining a radio); a long quiet
  time makes it bored, then asleep, which is never read as a radio fault.
- **Dashboard home screen** (Full, Dolphin Lab and Cyber Lab styles): the lab
  scene with the animated dolphin, the main menu with icons (Scan, Receive,
  Analyze, Database, Sessions, Dolphin Lab, Settings, About) and a status bar
  (frequency, preset, external radio, the active research session, a memory
  warning when a receive session would be refused, the edition). Up/Down
  move (wrapping), Left/Right a page, OK opens, long OK on Scan opens the
  Frequency Scanner and on Receive starts receiving and recording.
- **UI styles** (Settings > UI style): *Dolphin Lab* (default in Full),
  *Cyber Lab* (a radar, technical readouts, minimal mascot) and *Classic*
  (the Beta 7 menus and popups: no mascot, no animation, the lab view not
  even allocated). Saved in `settings.txt` (`Ui_style`, `Ui_feedback`,
  `Ui_transitions`); a Beta 7 file keeps working and gets the defaults. Below
  8 KB of free heap at start the session runs in Classic.
- **Cards** (lab styles): messages, busy and progress screens, save results
  and analysis reveals are drawn with the dolphin beside the text when it fits
  (else across the screen, never shortened). A progress card shows a
  percentage and bar only when the work reports one. Every message the app
  shows is checked on the host to fit whole.
- **Reactions to real events**: Scanner, Range Scanner and Waterfall hits,
  Receive and Hopper decodes and activity, recording starts and stops,
  analysis start, progress and completion, saves, refusals for memory and
  errors. A small badge in the radio screens' headers (the dolphin's head:
  calm, alert, listening, recording lamp; a radar in Cyber Lab) follows the
  same state.
- **Transitions** (Settings > Transitions): an oscilloscope-line wipe into
  the dashboard (once per run), into analysis reports (a 0.7 s reveal of the
  report's first lines, any key skips it), into the Waterfall and between the
  lab room's stations. Driven by the existing 100 ms tick, never blocking; a
  key ends a transition at once and is handled as usual.
- **Dolphin Lab room** (Full, a module of 8,830 B loaded only while open):
  a radar (an illustration, labelled so; OK opens the real Scanner), an
  oscilloscope showing a saved RAW recording's first 512 samples as recorded,
  an antenna (OK: Receive), a recorder (the number of recordings; OK: the
  Database), a terminal (recordings, saved reports and sessions counted on the
  SD card, the firmware family and version, the app version) and a gallery of
  every pose. The room never uses the radio.
- **UI feedback** (Settings > UI feedback: Off, Minimal, Full): Minimal adds
  a short vibration when a recording starts and when an analysis completes;
  Full adds short sounds, a menu click and a blip on detected activity (at
  most one every 2 s). Saves and errors keep their existing sounds and get
  nothing more. Every cue is rate limited (`rg_ui_feedback`). Off (the
  default) leaves the app as Beta 7.
- **Scanner**: a mark on the line under the header follows the frequency
  being measured (both editions). **Range Scanner**: the sweep marker is
  larger while sweeping, and the cursor has a cap.
- **Tests**: `test_mascot` (state machine, priorities, deadlines, clock wrap,
  styles, feedback rate limits, transitions, dither, menu window, text wrap,
  firmware family, sprites), `test_ui_lab` (the dashboard in both styles for
  every state, every app message on a card, progress, saves, transitions,
  badges on the Scanner, Receive, Range Scanner and Waterfall screens, the
  room), a `fuzz_ui` target, Beta 7 settings files in the format tests, and
  host canvas support for circles, discs, rounded boxes and bitmap
  transparency as the firmware draws them.

### Changed
- The module API version is 2 (the Waterfall's screen gained the badge and
  the entry reveal); a stale `.fal` is refused as before.
- The receiver's separator lines end at the screen's last column.

### Measured
- Resident code (allocated ELF sections, from the built files): Catalog
  77,782 B (beta 7: 77,172 B), Full 119,428 B (beta 7: 104,388 B). The lab
  room module is 8,830 B and the Waterfall module 4,315 B (was 2,740 B),
  each only while open. The dashboard frame draws in about 30 µs on the host
  (a wipe adds about 15 µs, the dither 25 µs): host figures for comparing the
  kinds, not device timings.

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

[Unreleased]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.11...HEAD
[1.0.0-beta.11]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.10...v1.0.0-beta.11
[1.0.0-beta.10]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.9...v1.0.0-beta.10
[1.0.0-beta.2]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.1...v1.0.0-beta.2
[1.0.0-beta.1]: https://github.com/mrSlime-man/RADIOGEDDON/releases/tag/v1.0.0-beta.1
