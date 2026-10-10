# Features

The complete list of what RadioGeddon does, how far each feature has been
verified, and its known limits. For step-by-step use, see the
[User Guide](USER_GUIDE.md).

## Verification status at a glance

**Every feature below is implemented, compiles for all three firmware
families, and passes CI. None has yet been verified on a physical Flipper
Zero.** "Unit-tested" means the feature's firmware-independent logic is covered
by the host test suite (532 checks, plus format tests on real files); radio behaviour can only be confirmed on a
device ([VERIFICATION.md](VERIFICATION.md)).

| Feature | Implemented | Unit-tested logic | Verified on hardware |
|---------|:-----------:|:-----------------:|:--------------------:|
| [Sub-GHz Scanner](#sub-ghz-scanner) | ✅ | ✅ floor, activity, CSV rows | ⏳ pending |
| [Frequency Hopper](#frequency-hopper) | ✅ | ✅ dwell, hold, lock, history | ⏳ pending |
| [RAW Signal Capture](#raw-signal-capture) | ✅ | ✅ ring, file format, writer thread with slow/failing card | ⏳ pending |
| [Protocol Identification](#protocol-identification) | ✅ | — (firmware decoders) | ⏳ pending |
| [Signal Analyzer](#signal-analyzer) | ✅ | ✅ parsing, clustering | ⏳ pending |
| [Unknown Protocol Analysis](#unknown-protocol-analysis) | ✅ | ✅ PWM/PPM/Manchester, noise, alignment, streaming | ⏳ pending |
| [Pulse Timeline](#pulse-timeline) | ✅ | ✅ layout, pan/zoom, frame navigation | ⏳ pending |
| [Signal Comparison](#signal-comparison) | ✅ | ✅ RAW similarity, pattern alignment | ⏳ pending |
| [Device ID Candidate Detection](#device-id-candidate-detection) | ✅ | ✅ constant/changing fields | ⏳ pending |
| [Rolling Code Classification](#rolling-code-classification) | ✅ | ✅ field-map logic | ⏳ pending |
| [Cryptographic Structure Heuristics](#cryptographic-structure-heuristics) | ✅ | ✅ key-byte statistics | ⏳ pending |
| [Signal Database](#signal-database) | ✅ | ✅ index, duplicates, sort/filter/search | ⏳ pending |
| [Authorized Signal Replay](#authorized-signal-replay) | ✅ | — | ⏳ pending |
| [Memory diagnostics](#memory-diagnostics) | ✅ | ✅ bookkeeping, Database and recorder lifecycles | ⏳ pending |

## Sub-GHz Scanner

A narrowband RSSI scanner. The CC1101 measures one frequency at a time, so the
scanner steps through a list of frequencies and shows activity on each; it is
not a wideband spectrum analyzer and cannot see the whole band at once.

- Live RSSI per frequency with a **peak hold**, a per-frequency **noise-floor
  estimate**, and an overall noise floor in the header.
- **Activity detection** at an adjustable threshold above each frequency's own
  floor (+6 to +30 dB) with 3 dB hysteresis, a live activity dot and a count of
  separate bursts per frequency.
- **Configurable scan list**: all 19 frequencies, a band (300-348, 387-464 or
  779-928 MHz), or any custom selection.
- **Configurable dwell** (5-100 ms per frequency); the strongest reading in
  each dwell window is kept so short bursts are less likely to be missed.
- **Hold on hit** (optional): stop on the first active frequency and highlight
  it; OK opens *Receive & Record* there.
- **Save results** to the SD card as CSV.
- The sweep runs on its own thread, so the display stays responsive.
- Frequencies (MHz): 300.000, 303.875, 304.250, 310.000, 315.000, 318.000,
  390.000, 418.000, 433.075, 433.420, 433.920, 434.420, 434.775, 438.900,
  464.000, 779.000, 868.350, 915.000, 925.000.
- Receive-only.

## Frequency Hopper

Continuous receive while hopping across a list of frequencies (by default
**315.00, 390.00, 433.92 and 868.35 MHz**), built to catch transmissions rather
than to hop fast.

- **Customizable hop list**: Common, All, a band, or any custom selection.
- **Adjustable dwell** (100 ms to 1 s) and **activity hold** (1 to 10 s).
- **Noise-floor-aware, adaptive detection**: each frequency learns its own
  floor; activity is a rise of the Threshold above it (3 dB hysteresis). A
  decoded parcel also counts as activity, so weak but decodable signals hold
  the hopper too. RSSI is sampled every 10 ms.
- **Hold** on activity, extended while the signal stays up or keeps decoding.
- **Lock / unlock** (pause and resume hopping) and **step to next** by hand.
- **Statistics**: per-frequency activity count, decodes, peak, floor and active
  time, plus the last 16 activity periods.
- **Optional automatic RAW recording** of each activity period, saved as its
  own `.sub` without overwriting existing files.
- Decoded signals are listed and saved exactly as in *Receive & Record*.
- Runs on its own thread; retunes the running receiver without powering the
  radio down, using the same stop/retune/start sequence as the firmware's own
  hopper. A recording never spans a retune.

## RAW Signal Capture

Records the raw on/off timing stream of a transmission — useful for protocols
the firmware can't decode — and saves it as a standard RAW `.sub` file
(`Filetype: Flipper SubGhz RAW File`), readable by the stock Sub-GHz app.

- **Streamed to the SD card while recording**, so a recording's length is
  limited by the card, not by RAM. It is written to a temporary file and
  renamed when you save it; Back on the name screen deletes it.
- The radio thread never waits for the card: it appends each pulse to a
  lock-free buffer (4 to 32 KB, sized to the free heap when recording starts)
  and a writer thread empties it into `RAW_Data` lines.
- If the card falls behind for longer than the buffer lasts, new samples are
  dropped and counted. The REC line shows `lost <n>` (or `buf <n>%` once the
  buffer is half full), the file ends with a `# Lost: …` comment that other
  tools ignore, and Unknown Protocol Analysis reports the count.
- The REC line shows the recording time and sample count; the result screen
  after saving shows the file name, sample count, duration and losses.
- A failed SD write stops the recording and is reported; it is not saved.
- The file layout is the firmware's own, so the stock Sub-GHz app opens and
  replays it.
- Uses the frequency and modulation chosen in **Settings**.

## Protocol Identification

While receiving (fixed frequency or hopping), every pulse is passed to the
firmware's own protocol decoders — all protocols your firmware marks as
decodable, such as Princeton, CAME, Nice FLO, Holtek and KeeLoq-family
protocols. Matches are `[CONFIRMED]` identifications and can be saved as
standard `.sub` key files (protocol, bit count, key). KeeLoq manufacturer names
need the firmware's keystore on the SD card, which RadioGeddon loads when
present.

Identification is only as broad as your firmware's decoder library, which
differs between Official, Unleashed and RogueMaster.

## Signal Analyzer

*Signal Info & Analysis* summarises a saved recording (protocol, frequency,
modulation, bit length and key, or sample count and pulse range) and adds:

- for RAW captures: `[HEURISTIC]` pulse-width groups with counts and an
  estimated base time unit (Te);
- for decoded protocols: bit and byte length, the key, and a `[HEURISTIC]` hint
  about the low bits that often encode the button.

Details: [Protocol Analysis](PROTOCOL_ANALYSIS.md#signal-info--analysis).

## Unknown Protocol Analysis

Streams the whole RAW capture through RadioGeddon's signal engine (about 8 KB
of RAM however long the file is) and reports in two labelled parts:

- `[OBSERVED]`: sample count and duration, high and low timing peaks, noise
  share, jitter, a quality grade, and frames cut on long gaps;
- `[HYPOTHESIS]`: base Te; the encoding (PWM, PPM or Manchester) chosen by
  trial-decoding every frame, with a confidence and the runner-up; bit length;
  repeated frame patterns in binary and hex, with frames that only match after
  a shift (a cut-off first frame) aligned to their pattern; a
  constant-vs-changing field map; and a device ID candidate.

A frame list ends the report: each frame's start time, bit count and pattern.

Unit tests cover PWM, PPM and Manchester identification, noise and jitter
robustness, cut-off frames, several patterns in one file and chunked
streaming, all on synthetic signals. Details:
[Protocol Analysis](PROTOCOL_ANALYSIS.md#unknown-protocol-analysis--observed-and-hypothesis).

## Pulse Timeline

A graphical view of a RAW capture: the waveform with frame-start markers,
pulse durations, an overview bar and the current frame and sample index.
Left/Right pan, Up/Down zoom between 5 µs and 5 ms per pixel, OK and hold OK
step between frames. Only a window of about a thousand samples is in memory;
the rest is streamed from the SD card as you move, using seek checkpoints
recorded while the file is first read. Details:
[User Guide](USER_GUIDE.md#pulse-timeline).

## Signal Comparison

*Compare with…* puts two recordings side by side, marking each field as the
same (`=`) or different (`~`): protocol, frequency, and the key for decoded
protocols, with a `[HEURISTIC]` key-difference hint for same-protocol pairs.
For two RAW captures it adds:

- a **timing-match score (0–100 %)** with a verdict: near-identical (≥ 90),
  similar structure (≥ 60), or clearly different. It compares the captures
  sample by sample from their start without alignment, so captures that begin
  at different points score lower even for the same button;
- a `[HYPOTHESIS]` **pattern comparison** of each file's dominant frame,
  aligned by up to 4 bits, which does not depend on when recording started.

Both stream the files, so long recordings are compared in full.

## Device ID Candidate Detection

When a RAW capture contains frames that differ in some bits (for example
several presses of a rolling-code remote), *Unknown Protocol Analysis* offers
the longest run of at least 8 constant bits as a hexadecimal **device ID
candidate**. Fixed remote or device identifiers usually live in that constant
part of the frame. It is a `[HYPOTHESIS]`: a candidate to investigate, not a
verified serial number.

## Rolling Code Classification

Two complementary signals:

- **Decoded protocols** — *Crypto Analysis* reports the firmware registry's
  classification: `[CONFIRMED] Static code`, `[CONFIRMED] Dynamic code`
  (rolling, KeeLoq-style) or `[CONFIRMED] Telemetry`.
- **Unknown protocols** — *Unknown Protocol Analysis* marks bits that change
  between frames; changing bits are reported as a possible counter, button
  code or encrypted data (`[HYPOTHESIS]`). Record several
  presses in one RAW capture to see this — frames within a single press are
  usually identical.

## Cryptographic Structure Heuristics

For decoded protocols, *Crypto Analysis* adds `[HEURISTIC]` key-byte
statistics (non-zero and distinct bytes; six or more distinct bytes is noted as
*possibly encrypted*), and *Compare* shows the key difference between two
captures of the same protocol.

**What it does not do:** RadioGeddon itself never recovers or guesses keys,
decrypts payloads, predicts rolling codes, or brute-forces anything. (The
firmware's own decoders, whose output RadioGeddon displays, may use the SD-card
manufacturer keystore to identify KeeLoq-family signals.)

## Signal Database

- Recordings live in `/ext/apps_data/radiogeddon/signals` on the SD card
  (created on first launch) as standard `.sub` files, so they work with the
  stock Sub-GHz app and other tools — and `.sub` files from elsewhere can be
  copied in for analysis.
- Saved files get a timestamped default name (`RG_YYYYMMDD_HHMMSS`) that you can
  edit; empty names are rejected.
- **Database list** of every `.sub` in the folder with its type (protocol,
  `RAW`, other `.sub` kind, or damaged), frequency and date, read from the
  first 512 bytes of each file.
- **Sort** by date (newest first), name, frequency or protocol; **filter** to
  RAW, decoded, a single protocol, duplicates or damaged files; **search**
  names (case-insensitive substring).
- **Duplicate detection**: decoded signals with the same protocol, frequency,
  bit count and key; RAW captures with identical contents (compared by size,
  then a 32-bit hash of the whole file, only where sizes match).
- **Damaged files** (whose first line is not a Flipper Sub-GHz `Filetype`) are
  listed as `BAD` instead of being hidden; Sub-GHz files without a protocol
  are listed as `?`.
- Memory is sized to the folder (about 60 bytes a file, up to 500 files) and
  kept within the free heap; a folder too large to fit is indexed in part and
  says so (`Files indexed: N of M`). The index exists only while the Database
  is open.
- Open a file to analyse, compare, replay or delete it; deleting asks for
  confirmation and the list is re-read afterwards.
- **File details**: size, date, type, frequency, preset, samples or bits, and
  the names of its duplicates.
- **Rename** from the device. Names the FAT file system can't store, and names
  already used, are refused, so a rename never replaces another file.
- **Report export**: *Save report to SD* writes the file's reports (Info &
  Analysis plus Unknown Protocol Analysis or Crypto Analysis), with their
  labels and a legend, to `apps_data/radiogeddon/reports/<name>.txt`, never
  replacing an existing report.
- **Damaged files** open with File details (showing the start of the first
  line), Rename and Delete only, instead of an error tone.
- The receiver keeps up to 32 decoded signals per session, listing identical
  consecutive repeats once.

## Authorized Signal Replay

Transmits a saved recording for testing devices you own or are authorized to
test.

- **RAW captures** are streamed exactly as recorded. A RAW capture skips the
  rolling-code check, so a RAW capture of a rolling-code remote *is* transmitted
  — but it only re-sends one code the receiver has already seen.
- **Decoded static protocols** are re-generated by the firmware's encoder and
  sent as a short burst (10 repeats), like one button press. The saved file is
  never modified.
- The recording's **own modulation** is used, including stock-app files that
  store a custom CC1101 register set (`Custom_preset_data`).

Replay **refuses** to transmit when:

| Condition | Message |
|-----------|---------|
| the protocol is dynamic (rolling code), not transmittable, or unknown to the firmware | `Protected/rolling code` |
| the firmware's region settings forbid the frequency | `Blocked by region` |
| the file is unreadable or its modulation preset isn't recognised | `Unsupported file` |

Regional rules are enforced by the firmware itself
(`subghz_devices_set_tx`), and RadioGeddon does not bypass them.

## Memory diagnostics

- **About** shows the free heap now, the largest free block, the free heap at
  app start, the lowest the app saw and the step before it, the app's peak
  use, the lowest since boot, the total heap, and what a receive session took
  ([User Guide](USER_GUIDE.md#about)). All figures come from the firmware's
  heap counters; nothing is estimated.
- **Receive and Hopper** measure what starting the radio took and, the next
  time, refuse with `Not enough memory` when that plus 6 KB is no longer free,
  instead of risking an out-of-memory crash. The measurement is kept per
  firmware.
- A one-line memory summary goes to the log when the app closes.

## Not implemented / out of scope

| Item | Status |
|------|--------|
| On-device verification of all features | Next milestone — [hardware checklist](HARDWARE_CHECKLIST.md) |
| Custom frequency entry, custom modulation entry | Planned — currently a fixed table and four presets |
| Decoders beyond the firmware's own library | Not planned for now — identification relies on the firmware |
| Desktop companion application | Deferred; files stay standard `.sub` so one can be built later |
| Key recovery, rolling-code bypass, brute force, jamming | **Never** — out of scope by design |

## Known limitations

- **Hardware verification pending** — see above.
- **RAW capture length** — limited by the SD card. A slow card with very noisy
  input can fall behind and lose samples; they are counted and reported, not
  hidden. Analysis and similarity read the whole file.
- **External radio** — only CC1101 modules through the firmware's
  `cc1101_ext` driver (a plugin on the SD card, installed with the firmware);
  untested on hardware so far.
- **Scanner and hopper sampling** — the radio hears one frequency at a time, so
  a transmission on a frequency the sweep is not currently on can be missed.
- **Firmware coupling** — each `.fap` only loads on firmware with the matching
  API major version ([Firmware Compatibility](FIRMWARE_COMPATIBILITY.md)).
