# Verification Status

What has been verified, how, and what has not. Integrity rule: nothing is
marked hardware-verified without evidence from a physical device. As of
`1.0.0-beta.7`, the only physical-hardware results are tester reports on
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
| Catalog edition, Official build | SDK 1.4.3 pinned by SHA-256, API asserted 87.1, manifest (name `RadioGeddon`, version) verified, all 289 imports exported by the SDK | Pass — `radiogeddon-catalog-official.fap` |
| Full edition, RogueMaster build | RogueMaster source at commit `38d7ae9`, built with its own `fbt`, API asserted 88.16, manifest (name `RadioGeddon Full`) verified, all 303 imports exported by its `api_symbols.csv` | Pass — `radiogeddon-full-roguemaster.fap` |
| Full edition, Momentum build | Momentum mntm-012 SDK pinned by SHA-256 (matches Momentum's update index), API asserted 87.1, manifest verified, all 303 imports exported by Momentum's SDK | Pass — `radiogeddon-full-momentum.fap` |
| Full edition, Unleashed build | SDK unlshd-093 pinned by SHA-256, API asserted 88.9, manifest verified, all 303 imports exported by the SDK | Pass — `radiogeddon-full-unleashed.fap` |
| Edition switches | `test_edition` built three times: as Full, as Catalog and with no edition (must be Catalog); defining both must not compile | Pass — 12 checks each, conflict refused |
| Lint | `ufbt lint` (clang-format) | Pass, no warnings |
| DSP/parse unit tests | `make -C test check` → `test_dsp` | Pass — 36 checks |
| Analysis-engine unit tests | `make -C test check` → `test_analyzer` | Pass — 137 checks |
| Scanner-logic unit tests | `make -C test check` → `test_scan` | Pass — 31 checks |
| Hopper-logic unit tests | `make -C test check` → `test_hop` | Pass — 42 checks |
| RAW-reader unit tests | `make -C test check` → `test_raw` | Pass — 43 checks |
| Timeline-maths unit tests | `make -C test check` → `test_timeline` | Pass — 42 checks |
| Sample-ring unit tests | `make -C test check` → `test_ring` | Pass — 47 checks |
| RAW-writer unit tests | `make -C test check` → `test_rawfmt` | Pass — 18 checks |
| Recorder tests (stub Furi/Storage) | `make -C test check` → `test_recorder` | Pass — 34 checks |
| Database-index unit tests | `make -C test check` → `test_db` | Pass — 75 checks |
| Memory-bookkeeping unit tests | `make -C test check` → `test_memstat` | Pass — 31 checks |
| Database-loading tests (stub Furi/Storage) | `make -C test check` → `test_dbload` | Pass — 45 checks |
| RAW-to-decoder feeding tests | `make -C test check` → `test_decode` | Pass — 68 checks |
| Frequency text and range tests | `make -C test check` → `test_freq` | Pass — 32 checks |
| Custom preset check | `make -C test check` → `test_preset` | Pass — 20 checks |
| Bands and range plans | `make -C test check` → `test_range`: band probing against Official 1.4.3's and RogueMaster/Momentum/Unleashed's real `furi_hal_subghz_is_frequency_valid()` ranges, range plans across gaps (every point tunable), limits, overflow, fine stepping | Pass — 96 checks |
| Transmit check | `make -C test check` → `test_txpolicy`: all 16 fact combinations in both editions, refusal texts | Pass — 272 checks |
| Range-scan display maths | `make -C test check` → `test_spectrum`: compact points, median floor over all points, column binning for 1 to 256 points | Pass — 450 checks |
| Checksum hypotheses | `make -C test check` → `test_checksum`: CRC-8 catalogue vectors, XOR / sum / CRC-8 / parity structures found, 200 random trials with no false fit, repeats and uncovered differences refused | Pass — 27 checks |
| Waterfall history | `make -C test check` → `test_waterfall`: ring wraparound and order, column/point mapping for 1 to 256 points, strongest point per column, levels over the floor and the solid threshold, missing cells never drawn as a level, dither density, pause and resume, pixel render | Pass — 172 checks |
| Bitstream Explorer maths | `make -C test check` → `test_bits`: bit and byte extraction from every offset, leftover bits, diff markers (`.`, `X`, `?`) with cut-off and noise frames excluded, field values to 64 bits | Pass — 59 checks |
| Multi-capture comparison | `make -C test check` → `test_multi`: synthetic sets with known constant, button, counter and data fields, frequency / preset / Te consistency, identical-frame letters, the noise check, labels on every report line, no "serial" claim | Pass — 48 checks |
| Research session files | `make -C test check` → `test_session`: format round trip, damaged lines and names, a failure at each step of a save (write, read-back, rename: the last complete version always loads), backup recovery, add / remove / rename, name rules, grouping suggestions | Pass — 91 checks |
| Module size estimate | `make -C test check` → `test_elf`: allocated-section sum of an ELF file, ELF64, big-endian, truncated and malformed headers refused | Pass — 10 checks |
| Offline UI | `make -C test ui` → `test_ui`: screens drawn with the firmware's own canvas fonts (u8g2 data from the pinned SDK) on a 128x64 host canvas; no text off screen or overlapping, keys driven through the Waterfall and Bitstream Explorer, the Database list with no, long-named and 200 files; synthetic previews written as PNG (CI artifact `synthetic-ui-previews`, labelled synthetic, not hardware screenshots) | Pass — 141 checks |
| Format tests (firmware code, real files) | `make -C test formats` → `test_formats`: the firmware's FlipperFormat and stream code and its 85 Sub-GHz test files, from the commit of Official 1.4.3 | Pass — 654 checks (also settings shared between editions and with beta 5, favorites and scan profiles) |
| Analyzer on real captures | `make -C test captures` → `test_fwanalyze`: `rg_analyzer` on the firmware's 50 paired RAW test captures, scored against each protocol's decoder source | Pass — 354 checks; Te right for 50, encoding family for 43, frame length for 35 (see below) |
| Decoder tests (firmware code, real captures) | `make -C test decoders` → `test_fwdecode`: the firmware's Sub-GHz receiver and all its protocol decoders, fed its 50 RAW test captures through `rg_decode` | Pass — 263 checks; all 50 decode and are described as the app describes them, and every decode saved as the app saves a key names its preset (see below) |
| Preset check on the firmware's presets | `make -C test decoders` → `test_fwpreset`: `rg_preset_check` on the firmware's six built-in CC1101 presets and the custom presets in its example settings file | Pass — 26 checks; all eight pass and end where the firmware ends them |
| Engine lifecycle | `make -C test lifecycle` → `test_lifecycle`: the real Scanner, Range Scanner (with and without a waterfall history) and Hopper engines started and stopped repeatedly on real threads against a fake radio, every allocation counted | Pass — 36 checks; memory unchanged between cycles and fully returned on free, every scan session and capture closed, no probe inside a band gap, activity detected after calibration, hold on hit |
| Apps Catalog rules | `scripts/check_catalog.py --catalog`: fam fields, icon, description and changelog through the catalog's own Markdown filter, ASCII-only strings, writes only under `apps_data/radiogeddon` | Pass; screenshots missing (need hardware) |
| Apps Catalog bundler | `scripts/catalog_bundle.py`: the catalog's own `tools/bundle.py` steps on the pushed commit (clone, `ufbt lint`, build, manifest from `application.fam`, path, includes, icon, values, Markdown) | Every step passes except screenshots (see "Not verified") |
| Fuzz corpus replay | `make -C test check` → `replay_fuzz_raw`, `replay_fuzz_db`, `replay_fuzz_samples`, `replay_fuzz_session` | Pass — every committed input |
| Fuzzing | `make -C test fuzz` (libFuzzer with ASan/UBSan; 60 s per target in CI) | Pass — no crash, sanitizer report or broken invariant |
| Static analysis | `scripts/static_analysis.py`: GCC `-fanalyzer` and clang-tidy ([`.clang-tidy`](../.clang-tidy)) over the device code with the build flags: the Catalog edition on the Official SDK, the Full edition on the Momentum and Unleashed SDKs | Pass — 0 findings (beta 7: 19 findings in the new code fixed first, two of them real uninitialised-value paths) |
| Thread safety of ring and recorder | `make -C test tsan` (ThreadSanitizer; optional, not in CI) | Pass — no reports |
| Memory safety of tested code | tests built `-Werror` under `-fsanitize=address,undefined` | Pass — no ASan/UBSan reports |
| Documentation links | `scripts/check_links.py` (offline link + anchor check) | Pass |
| Release metadata | `scripts/release_meta.py`: version in `radiogeddon_version.h` and `application.fam`, release notes, CHANGELOG section, the API listed for each `.fap` against the pins, download links | Pass |
| Newer firmware (canary) | `scripts/firmware_watch.py --build` (weekly in CI): Catalog edition on the Official 1.5.1-rc SDK, API 88.2 | Builds; `APPCHK`, manifest and imports pass. No release targets it yet, not hardware-tested |
| `.fap` metadata | `scripts/verify_fap.py` parses `.fapmeta` and asserts magic, API, target, name, version, icon; with `--symbols`, that every undefined symbol in the ELF symbol table is exported by the SDK | Pass for all four artifacts |
| Resident code size | `scripts/fap_size.py`: the sum of the allocated (`SHF_ALLOC`) ELF sections of the built files, which is what the loader places in the heap | Catalog 77,173 B (beta 6: 75,704 B, same script), Full 104,388 B (beta 6: 94,676 B); Full modules, loaded only while in use: Waterfall 2,740 B, Bitstream 6,484 B, Unknown 15,608 B, Multi 15,960 B, Sessions 16,383 B |
| Embedded modules | `scripts/verify_fap.py --modules`: unpacks the Full `.fap`'s `.fapassets`, checks each of the five `.fal` plugins' manifest and that every symbol it imports is exported by that firmware's SDK | Pass for all three Full builds |

Host-test total: **1,962 checks, 0 failures** (`make -C test check`; the format, capture and decoder tests are counted on their own). What the suite covers (synthetic
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
  degenerate input (empty, one pulse, one level only, pure noise). Five
  cases come from what the real captures showed: a glitch peak larger than
  the signal's never becomes Te, low-first PWM (CAME's) pairs low then high,
  a square-wave preamble does not outvote the data, repeats 5 Te apart are
  split, and so are repeats separated by a long carrier pulse. Each of the
  five fails on the engine before those rules.
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
- `test_decode` — feeding a RAW capture to decoders (`rg_decode.c`): sign
  to level and duration (including the int32 extremes), every sample in
  order whatever the read size, commas and skipped bad values, time and
  sample counts kept current, decodes timed at the start of the sample that
  completed them, repeats counted once with first and last time, `\r`
  removed, long names and descriptions cut, a full list still counting
  repeats and dropped entries, and progress reports. The decoder is a 24-bit
  PWM stand-in written for the test, fed synthetic frames with noise before
  them; mutating the level mapping or the timing order makes it fail. The
  firmware's real decoders are `test_fwdecode`'s.
- `test_freq` — frequency text: two decimals for whole 10 kHz steps, three
  otherwise (433.075 MHz is no longer shown as 433.07), digits below 1 kHz
  dropped, short buffers; the range a saved or typed frequency must be in
  (281-962 MHz, bounds included); the nearest list entry to a custom
  frequency. Whether the radio tunes a frequency is the firmware's check and
  is not tested on the host; `test_formats` checks that a custom frequency
  is kept in `settings.txt` and an out-of-range one is not.
- `test_preset` — the custom preset check (`rg_preset.c`) that Replay runs
  before the firmware loads a file's `Custom_preset_data`: a stock-shaped
  preset, an empty register list and trailing bytes pass; no data, a list
  cut before or inside its `00 00` end, and every cut inside the 8-byte PA
  table are refused; every address above `0x2E` (strobes such as STX, the
  PA table, the FIFO) is refused and every configuration register
  `0x01`-`0x2E` allowed; a strobe-valued data byte, or PA bytes after the
  end, are not taken for addresses. The presets are written for the test;
  the firmware's own are `test_fwpreset`'s.

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
- `fuzz_session` — a research session file of any content, and a module
  file for the size estimate: a parsed session has a valid name and at most
  24 distinct `.sub` names, writing it and parsing it again gives the same
  session, and the ELF estimate never reads outside the file. It found that a
  session name ending in a space did not survive a save and load; fixed, and
  the input is in the corpus.

The seed corpus is synthetic (the fixtures plus hand-made edge cases, written
by `test/fuzz/make_seeds.py`) plus inputs the fuzzer found.

`test_formats` (`make -C test formats`) and `test_fwdecode` (below) are the
suites that use files from outside this repository. `test/firmware/fetch.sh`
downloads the Flipper Zero firmware's FlipperFormat and stream code and the 85
`.sub` files of its own Sub-GHz unit tests, at the commit of the pinned
Official release, and checks each against `test/firmware/files.sha256`. They
are GPL-3.0 and only used here to build and run the tests, never committed or
shipped. The firmware's code is compiled unchanged over the stub Furi/Storage
layer, so RadioGeddon's settings and `.sub` loading run against the same
parser as on the device. Those files are third-party test data: they were not
captured or checked on our hardware. The suite covers:

- every file loading with the kind, protocol, frequency, preset, bit count,
  key, RAW sample count and shortest/longest duration that an independent
  reading of the file finds; the Database index and the streaming RAW reader
  agreeing with it; its preset being one Replay finds by name (all 85); the
  analyzer completing on all 51 RAW files with figures in range; and every
  byte and file handle returned;
- settings saved and read back field by field, written as the firmware
  writes Flipper Format; a custom frequency kept and one outside every
  firmware's range refused; out-of-range values, the limits themselves, extra
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
the decoded `Bit` count for 10 (Ansonic, CAME TWEE, Doitrand, Feron, GateTX,
Marantec, Nice FLO, Princeton, Security+ 2.0 and SMC5326) and is one bit
short for 7 more (BETT, Dooya, GangQi, Legrand, Linear, Mastercode and Roger,
where the last bit's low period runs into the gap between frames). It finds
no encoding for 4 of the 51 RAW files, and still takes some pulse-width
protocols (Star Line, Nero Radio, Holtek HT12X) for Manchester. The test
fails if a change lowers those 10 and 17, so an analyzer change cannot make
this worse unnoticed. `test_fwanalyze` (below) scores the analyzer on all 50
paired captures against the decoders' own timing.

`test_fwdecode` (`make -C test decoders`) fetches the firmware's Sub-GHz
receiver and protocol decoders the same way (plus the two M\*LIB headers they
use, at the firmware's submodule commit) and builds them unchanged. Its
environment is the app's: no keystore (a stand-in with no keys) and no
rainbow table names. Each RAW capture that the firmware's own decoder test
pairs with a protocol (the list is checked against that test) goes through
`RgRawReader` and `rg_decode_run` into a receiver with every decodable
protocol, as the app feeds it, is described with the app's own
`radiogeddon_decode_text`, and must produce that protocol in the decode log.
All 50 do. The test found that two decoders, CAME Atomo and Alutech AT-4N,
read their rainbow table's file name without checking it when they describe
a decode (`came_atomo.c:198`, `alutech_at_4n.c:86`). The app sets no tables,
so that name is NULL; the firmware blocks access to the first megabyte, so on
Official firmware Receive should crash when it decodes either protocol (up to
`v1.0.0-beta.3`; not checked on hardware). Unleashed and RogueMaster check
for a missing name. The app now describes those two from the decoded data
(name, bits, key); the test checks that, and that the decoders' own text
still crashes in the app's environment, so a firmware update that fixes it
shows up. None of the captures needs a keystore to decode. The firmware's random capture gives 327 decodes on a
fresh receiver and the firmware's 328 after its decoder tests have run on
the same receiver (a Holtek HT12X decoder keeps its last code between them).
Ending a capture with a quiet line changes no count. The decoded file next to
a capture is compared for information only: 17 of 34 keys also decode from
the capture; the others hold other codes (another button, counter or
remote).

Every decode of the expected protocol is also saved as the app saves a key
(`radiogeddon_decode_preset`, then the decoder's own serializer), once on
each of the app's four presets: 4,116 saves. Each must name its preset by
the name the stock app and Replay look up, and carry no custom preset. This
check came after reading the save code: up to `v1.0.0-beta.4` the app handed
the decoders the preset's file name (`FuriHalSubGhzPresetOok650Async`)
where the firmware expects its short name (`AM650`). The firmware writes any
name it does not know as a custom preset with the register list it is given,
here none (`lib/subghz/blocks/generic.c`, the same in Unleashed and
RogueMaster). So every key saved from Receive or the Hopper said
`Preset: FuriHalSubGhzPresetCustom` with an empty `Custom_preset_data`: the
stock app's loader refuses that, and Replay refused it as an unsupported
file. With the old name, all 4,116 saves fail the check. Opening a saved key
in the stock app on a device is hardware check F6g.

`test_fwpreset` (also run by `make -C test decoders`) compiles the
firmware's built-in CC1101 register arrays (`cc1101_configs.c`: AM 270,
AM 650, FM 2.38k, FM 47.6k, MSK and GFSK) and reads the two example custom
presets in its `setting_user.example`, and runs `rg_preset_check` on each:
all pass, and each is exactly its registers, the `00 00` end and the 8-byte
PA table, as the firmware loads it. So the check refuses nothing the firmware
itself ships. Whether a refused preset would really have started a
transmission on a device is not tested; the refusal happens before the
radio is touched either way.

`test_fwanalyze` (`make -C test captures`) runs the analyzer on the same 50
captures, streamed and rewound for each pass as the app does, without telling
it the protocol. It reads each protocol's decoder source at run time and
scores the engine's hypotheses: Te within the decoder's tolerance of its
short pulse (50 of 50), Manchester exactly when the decoder uses the
firmware's Manchester decoder (41; 3 get no guess), and frame length within
one bit of the decoder's bit count (33). Beta 5 scored 48, 41 and 29, and the
engine before this test existed 39, 33 and 16. The test also checks the
engine's own rules on every capture (Te is 0 or at least 140 µs, confidence at most 95 %, no bits
without an encoding) and that the streamed result equals the in-memory one.
It fails if a total drops. The misses are listed by
[PROTOCOL_ANALYSIS.md](PROTOCOL_ANALYSIS.md#accuracy-on-the-firmwares-test-captures):
pulse-width codes with equal pulse and gap per bit read as Manchester, and
frame lengths that include a long preamble or count bits differently.

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
- Custom frequency entry on a device (checklist F1f): the number keyboard,
  the firmware refusing frequencies its radio cannot tune, and receiving on
  a typed frequency. The keyboard and the radio check are firmware code and
  are not run by the host tests.
- *Decode with Firmware* on a device (checklist F4g): that the firmware's
  decoders find in a saved capture what Receive finds live, how long a long
  capture takes, and that the memory check and freeing hold on the firmware's
  heap. The host tests feed the firmware's decoders its own test captures
  through `rg_decode` (`test_fwdecode`), but they do not run this screen,
  the keystore or the device's heap.
- Keys saved from Receive and the Hopper opening in the stock Sub-GHz app on
  a device (checklist F6g). The host tests save them with the firmware's
  own serializer and read them back with its FlipperFormat code, but they do
  not run the stock app's loader.
- The internal-radio presence fix (defect 1) actually resolving "No radio" on a
  device.
- The analysis engine's inferences on signals captured with this app: the
  host tests use synthetic waveforms and the firmware's 50 RAW test captures
  (`test_fwanalyze`, recorded by others), not captures from a RadioGeddon
  device. How its noise, jitter and peak thresholds behave on the device's
  own receiver noise, and how long a whole-file analysis of a large capture
  takes on the SD card, are unchecked.
- Regional TX enforcement actually blocking disallowed frequencies on hardware,
  and Replay refusing a damaged custom preset on the device (checklist F7).
- Long-run memory stability and absence of radio-threading crashes. The
  About memory figures, the measured receive-session cost and the
  `Not enough memory` refusal are untested on a device (checklist F15–F15c);
  the host tests check the bookkeeping and the Database and recorder
  lifecycles, not the firmware's heap.
- That each per-firmware `.fap` loads and runs on its matching firmware —
  in particular the first Momentum build and both new editions (checklist
  L1–L4, E1–E3).
- The Full edition's Range Scanner, favorites, profiles, fine stepping and
  Radio bands screen on a device (checklist R1–R11): real sweep times, RSSI
  behaviour across bands, the band probe against the real drivers (including
  an external module's), and the spectrum screen's readability. The host
  tests run the engine against a fake radio, not the CC1101.
- The Catalog edition's region check refusing a transmission on a device
  (checklist C1–C3); the host tests check the decision for every combination
  of the firmware's answers, not the firmware's region data.
- Memory headroom of the larger Full edition on a device (checklist M1–M5):
  its resident code is about 31 KB larger than beta 5's (9.7 KB more than
  beta 6), and each module needs its own size free while in use.
- The beta 7 research tools on a device (checklist B1–B9): the Waterfall's
  real sweep rate and readability, module loading by each firmware's plugin
  manager, the Bitstream Explorer and Multi-Capture Compare on captures from a
  device, Research Sessions on a real SD card (including a power cut while
  saving), and the grouped Full menu. The host tests run the pure logic, the
  screens on a host canvas and the engine against a fake radio; they do not
  run the firmware's plugin loader, display or SD card.
- Apps Catalog acceptance: the submission needs qFlipper screenshots from a
  device, and the catalog's moderators decide.

## How this file is updated

After running the [hardware checklist](HARDWARE_CHECKLIST.md) on a device, move
each confirmed item into the "Verified" section with the date, firmware family
and version, and a one-line evidence note (log excerpt or observed behaviour).
Report results through the hardware-report issue form so they can be
corroborated.
