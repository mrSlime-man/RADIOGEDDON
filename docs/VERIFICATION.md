# Verification Status

What has been verified, how, and what has not. Integrity rule: nothing is
marked hardware-verified without evidence from a physical device. As of
`1.0.0-beta.3`, the only physical-hardware results are tester reports on
earlier code (see "Reported by users" below); no checklist item is
independently verified, and the "Not verified" section stands open.

## Environment

The project is developed and built in CI containers with no Flipper Zero
attached (no USB device, no `/dev/ttyACM*`). Every radio behaviour is therefore
unverified and is listed under "Not verified".

## Verified by automation (evidence-backed)

All of the following run in GitHub Actions on every pull request and every
release tag (`.github/workflows/ci.yml` → `build.yml`), and can be reproduced
locally.

| Area | Method | Result |
|------|--------|--------|
| Official build | SDK 1.4.3 pinned by SHA-256, API asserted 87.1, `.fap` manifest verified | Pass — `radiogeddon-official.fap` |
| Unleashed build | SDK unlshd-093 pinned by SHA-256, API asserted 88.9, manifest verified | Pass — `radiogeddon-unleashed.fap` |
| RogueMaster build | RogueMaster source at commit `38d7ae9`, built with its own `fbt`, API asserted 88.16, manifest verified | Pass — `radiogeddon-roguemaster.fap` |
| Lint | `ufbt lint` (clang-format) | Pass, no warnings |
| DSP/parse unit tests | `make -C test check` → `test_dsp` | Pass — 36 checks |
| Analysis-engine unit tests | `make -C test check` → `test_analyzer` | Pass — 89 checks |
| Scanner-logic unit tests | `make -C test check` → `test_scan` | Pass — 31 checks |
| Hopper-logic unit tests | `make -C test check` → `test_hop` | Pass — 42 checks |
| RAW-reader unit tests | `make -C test check` → `test_raw` | Pass — 43 checks |
| Timeline-maths unit tests | `make -C test check` → `test_timeline` | Pass — 42 checks |
| Sample-ring unit tests | `make -C test check` → `test_ring` | Pass — 47 checks |
| RAW-writer unit tests | `make -C test check` → `test_rawfmt` | Pass — 18 checks |
| Recorder tests (stub Furi/Storage) | `make -C test check` → `test_recorder` | Pass — 34 checks |
| Database-index unit tests | `make -C test check` → `test_db` | Pass — 75 checks |
| Memory-bookkeeping unit tests | `make -C test check` → `test_memstat` | Pass — 30 checks |
| Database-loading tests (stub Furi/Storage) | `make -C test check` → `test_dbload` | Pass — 45 checks |
| Format tests (firmware code, real files) | `make -C test formats` → `test_formats`: the firmware's FlipperFormat and stream code and its 85 Sub-GHz test files, from the commit of Official 1.4.3 | Pass — 562 checks |
| Fuzz corpus replay | `make -C test check` → `replay_fuzz_raw`, `replay_fuzz_db`, `replay_fuzz_samples` | Pass — every committed input |
| Fuzzing | `make -C test fuzz` (libFuzzer with ASan/UBSan; 60 s per target in CI) | Pass — no crash, sanitizer report or broken invariant |
| Static analysis | `scripts/static_analysis.py`: GCC `-fanalyzer` and clang-tidy ([`.clang-tidy`](../.clang-tidy)) over the device code with the build flags, Official and Unleashed SDKs | Pass — 0 findings |
| Thread safety of ring and recorder | `make -C test tsan` (ThreadSanitizer; optional, not in CI) | Pass — no reports |
| Memory safety of tested code | tests built `-Werror` under `-fsanitize=address,undefined` | Pass — no ASan/UBSan reports |
| Documentation links | `scripts/check_links.py` (offline link + anchor check) | Pass |
| Release metadata | `scripts/release_meta.py`: version in `radiogeddon_version.h` and `application.fam`, release notes, CHANGELOG section, the API listed for each `.fap` against the pins, download links | Pass |
| Newer firmware (canary) | `scripts/firmware_watch.py --build` (weekly in CI): Official 1.5.1-rc SDK, API 88.2 | Builds; `APPCHK` and manifest pass. No release targets it yet, not hardware-tested |
| `.fap` metadata | `scripts/verify_fap.py` parses `.fapmeta` and asserts magic, API, target, name, version, icon | Pass for all three artifacts |

Host-test total: **532 checks, 0 failures** (`make -C test check`; the format tests are counted on their own). What the suite covers (synthetic
signals, not real captures):

- `test_dsp` — RAW `RAW_Data` parsing (incl. whitespace, signs, out-of-range
  values saturating at 2,147,483,647),
  duration clustering, cluster sorting, and a RAW capture→file→reparse
  round-trip.
- `test_analyzer` — PWM, PPM and Manchester identification with exact bit
  recovery (both Manchester phases), Te estimation, timing peaks, noise share
  and quality grade, robustness to receiver-like noise and ±12 % jitter, frame
  grouping (identical repeats, two buttons, largest pattern first), alignment
  of a cut-off first frame, bit-length estimation, constant-vs-changing field
  maps and the ID candidate, identical results when fed in chunks with a split
  pulse, the decode and align APIs, streamed and in-memory RAW similarity, and
  degenerate input (empty, one pulse, one level only, pure noise).
- `test_scan` — scanner logic on synthetic RSSI sequences: no false triggers
  on noise, one count per burst, warm-up suppression, hysteresis, absolute
  minimum, floor tracking up and down, peak/count reset, median noise floor,
  band masks and CSV row formatting (including truncation).
- `test_hop` — hopper state machine on simulated time and RSSI: even time
  sharing on quiet bands, holding on a burst with no retunes until the hold
  expires, event history (channel, peak, duration), lock/unlock, decodes
  starting and extending holds, ring-buffer bounds, single/empty lists, and
  200 repeated hold/lock/retune cycles with no stuck state.
- `test_raw` — the streaming `RAW_Data` reader: values, signs, zeros, CRLF and
  a missing final newline, reads split at 5- and 13-byte and one-sample
  boundaries, corrupt tokens and lone minus signs, a comma after a value (as
  the firmware's RAW player allows), non-RAW files, a 6,000-sample file with one
  3,000-value line (bounded checkpoint table, seeking to any time resumes with
  the right samples, rewind), `test/fixtures/raw_ref.sub` read end to end
  through the analyzer, and the recorder's `# Lost: N` note (read once per
  pass, only in its exact form, also at end of file without a newline).
- `test_timeline` — pulse timeline maths: rasterising samples into columns
  (both levels in one column, data outside the window, a screen edge inside a
  pulse, times near 2^32 µs), duration labels only for wide, fully visible
  pulses, pan and zoom limits with the centre kept, the initial zoom choice,
  window coverage, and next/previous frame navigation at both ends.
- `test_ring` — the recorder's lock-free ring: capacity rules, order, refusal
  when full with lost-sample, gap and first-gap accounting, peak fill, index
  wrap at 2^32, heap-based sizing, and a two-thread run (2 million pushes
  against a stalling consumer) checking that every accepted value arrives
  once, in order and intact, and every other one is counted lost.
- `test_rawfmt` — the RAW writer: header byte-for-byte as the firmware writes
  it, 512 values a line with single spaces, zeros skipped, int32 extremes,
  identical output whatever the buffer size (24 bytes to 2 KB), the lost-sample
  note, and a 5,000-sample file read back exactly by the RAW reader.
- `test_recorder` — the recorder and its writer thread on pthreads and host
  files: a stream at the radio's top rate (about 32,000 samples/s) kept
  complete and in order; a card slowed to 20 ms a write losing samples in
  counted gaps while the producer never waits, with the count read back from
  the file; samples pushed before the file opens; a quiet recording still
  reaching the card; open, memory and write failures (no further writes, error
  reported); 40 back-to-back recordings alternating the largest and smallest
  ring, each staying within the heap it was given minus the 12 KB spare and
  returning every byte and file handle, and a refused recorder allocating
  nothing. Host timing is not the Flipper's: this shows the logic, not the
  device's throughput.
- `test_db` — the database index: decoded and RAW headers (frequency,
  protocol, bits, key hashed regardless of spacing or case), CRLF lines, a
  leading comment, an unreadable frequency, a long protocol name, a last line
  cut off at the 512-byte limit (not trusted), damaged files (empty, plain
  text, another `Filetype`, `Filetype` not on the first line, binary, a first
  line cut off), the name pool and entry table filling up, duplicates (a
  different frequency is not one; RAW only with equal size and content hash;
  stale marks cleared), every filter, the name search, each sort order with
  its tie-breaks, the protocol list, finding a file by name, and Rename's
  name rules (empty, longest allowed, too long, each character FAT cannot
  store, a dot or space at either end, a typed `.sub` in any case).
- `test_memstat` — memory bookkeeping: the lowest free heap and where, peak
  use (a later rise does not hide it), the fit check (an unmeasured cost
  never refuses, exactly cost plus margin fits, no overflow), when a new
  measurement replaces the stored one (over 1 KB), the session cost from free
  heap and low-water mark (steady cost, a transient dip, no underflow), and
  the firmware tag (stable, changes with version or hash).
- `test_dbload` — the Database loader (`radiogeddon_db.c`) on a real host
  folder through the stub layer, with every allocation counted: a missing
  folder; decoded, RAW, damaged (binary, empty) and duplicate files, `.SUB` in
  capitals, and skipped hidden files, other extensions and folders; sizes,
  times, filters and paths; progress once per file to 100 %; a sweep of the
  available heap from the 24 KB spare up showing refusal, partial and full
  indexes, the truncation flag matching, never fewer files with more memory,
  and peak use within the heap minus the spare (plus about 1 KB of handles);
  and 100 repeated loads with the same peak and nothing left allocated or
  open.

The fuzz targets (`test/fuzz/`) feed arbitrary bytes to the code that reads
files from the SD card and abort on any broken invariant, which libFuzzer and
the sanitizers then report:

- `fuzz_raw` — a `.sub` file of any content through the RAW reader with random
  read sizes, then the analyzer pass by pass: no zero samples, total time
  equal to the samples' sum, the same samples whatever the read sizes and
  after a rewind, seeks landing at or before the target with the right
  samples, and analysis figures inside their documented ranges.
- `fuzz_samples` — any int32 timing values (zeros and extremes included)
  through the analyzer, RAW similarity and Pulse Timeline: scores 0–100,
  frame bit strings matching their lengths, only the two level flags in
  columns, labels on screen, and the view kept inside the recording after
  clamp, pan and zoom.
- `fuzz_db` — file names and file headers of any content through the
  Database index, then every sort, filter and search, and one `RAW_Data` line
  through Signal Info's parser: known kinds, terminated protocol names,
  valid distinct indices of matching entries, "all" showing every file, and
  consistent counts, minimum and maximum. It found that a value beyond the
  int32 range was negated with undefined behaviour; fixed, with a unit test.

The seed corpus is synthetic (the fixtures plus hand-made edge cases, written
by `test/fuzz/make_seeds.py`) plus inputs the fuzzer found.

`test_formats` (`make -C test formats`) is the one suite that uses files from
outside this repository. `test/firmware/fetch.sh` downloads the Flipper Zero
firmware's FlipperFormat and stream code and the 85 `.sub` files of its own
Sub-GHz unit tests, at the commit of the pinned Official release, and checks
each against `test/firmware/files.sha256`. They are GPL-3.0 and only used
here to build and run the tests, never committed or shipped. The firmware's
code is compiled unchanged over the stub Furi/Storage layer, so RadioGeddon's
settings and `.sub` loading run against the same parser as on the device.
Those files are third-party test data: they were not captured or checked on
our hardware. The suite covers:

- every file loading with the kind, protocol, frequency, preset, bit count,
  key, RAW sample count and shortest/longest duration that an independent
  reading of the file finds; the Database index and the streaming RAW reader
  agreeing with it; the analyzer completing on all 51 RAW files with figures
  in range; and every byte and file handle returned;
- settings saved and read back field by field, written as the firmware
  writes Flipper Format; out-of-range values, the limits themselves, extra
  mask bits, keys in any order or missing, a session cost without its
  firmware, CRLF line ends, another file type or version, a file cut off
  mid-line, binary garbage and an empty file;
- a save that fails part-way (the previous settings are kept and the
  temporary file removed), firmware whose rename will not replace a file, and
  no card;
- writing a decoded signal and reading it back, unique names up to `_99`, a
  failed write leaving no damaged file, the default name, missing files;
- 100 saves and loads returning every byte and file handle.

It found that RAW files with a comma after each value (as in the firmware's
`hormann_hsm_raw.sub` test file) read as empty; fixed, with unit tests in
`test_raw` and `test_dsp`. Hardware check F4f covers it on a device.

It also measures the analyzer on real captures, without treating its output
as verified. For the 33 RAW files that have a decoded file of the same
protocol next to them, the most common frame length the analyzer finds equals
the decoded `Bit` count for 8 (Princeton, Feron, Dooya, GateTX, Holtek HT12X,
SMC5326, CAME TWEE and Security+ 2.0) and is one bit short for 5 more (BETT,
Legrand, Linear, Mastercode and Roger, where the last bit's low period runs
into the gap between frames). It finds no encoding for 6 of the 51 RAW files, and takes
some PWM protocols (CAME, Nice FLO) for Manchester. The test fails if a
change lowers those 8 and 13, so an analyzer change cannot make this worse
unnoticed.

## Release-pipeline integrity

- Releases are produced only from a version tag by `release.yml`, after the full
  build pipeline passes. The workflow stages assets in a draft, re-downloads
  them, checks them against `SHA256SUMS`, and only then publishes; any failure
  deletes the draft.
- Each `.fap` carries a signed build-provenance attestation
  (`actions/attest-build-provenance`) linking it to the workflow run and the
  tagged commit. Toolchain builds are not byte-for-byte reproducible across
  machines, so provenance — not cross-machine hash equality — is the integrity
  guarantee.

## Code review of the pre-merge branches

The two development branches were reviewed against the firmware sources during
the merge; defects found and fixed:

1. **Critical — internal radio reported absent.** Device presence had been
   gated on `subghz_devices_begin()`'s return value, which is `false` for the
   internal CC1101 (its interconnect `begin` hook is empty). On real hardware
   every radio screen would have shown "No radio". Fixed to use
   `subghz_devices_is_connect()` after an ignored-return `begin()`. This
   compiles and passes host tests either way, so only source review caught it —
   and it is itself a prime item for hardware confirmation (checklist W2).
2. **Replay modulation fidelity.** Replay reproduces the capture's exact
   modulation, including loading a custom CC1101 register array from a file's
   `Custom_preset_data`; an unrecognised preset is refused rather than
   transmitted on a default modulation.
3. **Destructive delete.** Deleting a recording now requires explicit
   confirmation.

Earlier stabilization fixes from the analyzer branch are retained: RX start/stop
ordering matching the firmware, the TX region gate via `subghz_devices_set_tx`,
replay no longer mutating the stored file, cross-thread GUI safety (worker
records to a mutex-protected history and posts an event), and invalid-frequency
guards before tuning.

## Reported by users on physical hardware

These are results reported by a tester, not independently reproduced or
logged by the project. They are recorded separately from the automated
evidence above and from the checklist's "Verified" bar.

| Date | Firmware | Build | Reported result |
|------|----------|-------|-----------------|
| 2026-10-09 | RogueMaster (version not recorded) | `v1.0.0-beta.1` | **Crash on launch**: "Flipper crashed and was rebooted — Out of memory", before the main menu. |
| 2026-10-09 | RogueMaster (version not recorded) | PR #3 (memory fix, later `v1.0.0-beta.2`) | The app launches to the main menu and works ("it is working"). Individual checklist items were not reported separately. |

The crash was caused by allocating about 110 KB of radio state (a 64 KB RAW
buffer, the protocol decoders and keystore, and the Sub-GHz worker) at app
start; see [Architecture](ARCHITECTURE.md). Checklist item W1 (launch) on
RogueMaster is therefore user-reported as passing; it still needs a logged
report through the hardware-report form to count as verified.

## NOT verified (requires physical hardware)

Everything about on-device radio behaviour, and the end-to-end workflow. See the
[hardware checklist](HARDWARE_CHECKLIST.md). In particular:

- Real over-the-air reception and live protocol decoding.
- RAW capture fidelity, and replay producing a working transmission.
- Streaming recording on a real SD card: sustained write rate, how often a
  slow card loses samples on noisy input, behaviour when the card is removed
  mid-recording, and that the stock Sub-GHz app opens and replays the files
  (checklist F6–F6e).
- The Database list on a real card: how long indexing takes for a large
  folder, memory left for analysis while it is open, and the list, options
  and re-read after a delete (checklist F11–F11e); rename, report export and
  damaged-file handling on the device (F12–F12d). The report writer and the
  rename checks against existing files use the SD card and are not covered by
  host tests.
- External CC1101 support: detection, the 5 V pin, receiving and the region
  check when transmitting through a real module (checklist F14–F14e). It was
  written against the official firmware's `cc1101_ext` driver source (1.4.3);
  Unleashed and RogueMaster builds compile against their own SDKs, but their
  drivers' behaviour is unverified.
- The internal-radio presence fix (defect 1) actually resolving "No radio" on a
  device.
- The analysis engine's inferences against real captured signals (host tests use
  synthetic waveforms only), including how its noise, jitter and peak
  thresholds behave on real receiver noise, and how long a whole-file analysis
  of a large capture takes on the SD card.
- Regional TX enforcement actually blocking disallowed frequencies on hardware.
- Long-run memory stability and absence of radio-threading crashes. The
  About memory figures, the measured receive-session cost and the
  `Not enough memory` refusal are untested on a device (checklist F15–F15c);
  the host tests check the bookkeeping and the Database and recorder
  lifecycles, not the firmware's heap.
- That each per-firmware `.fap` loads and runs on its matching firmware.

## How this file is updated

After running the [hardware checklist](HARDWARE_CHECKLIST.md) on a device, move
each confirmed item into the "Verified" section with the date, firmware family
and version, and a one-line evidence note (log excerpt or observed behaviour).
Report results through the hardware-report issue form so they can be
corroborated.
