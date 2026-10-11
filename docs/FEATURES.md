# Features

The complete list of what RadioGeddon does, how far each feature has been
verified, and its known limits. For step-by-step use, see the
[User Guide](USER_GUIDE.md).

## Editions

RadioGeddon is built as two editions from one source tree. Every difference is
a feature switch in [`radiogeddon_edition.h`](../radiogeddon_edition.h);
everything else — radio layer, recorder, decoders, analysis engine, storage,
settings — is the same code in both.

| | **Catalog** | **Full** |
|---|---|---|
| For | Official firmware; the Flipper Apps Catalog | RogueMaster, Momentum, Unleashed |
| Built by | a plain `ufbt` (`application.fam`) | `scripts/stage_edition.py full` |
| App name, appid | RadioGeddon, `radiogeddon` | RadioGeddon Full, `radiogeddon_full` |
| Every feature in the table below not marked *Full* | ✓ | ✓ |
| [Range Scanner](#range-scanner-full-edition) and scan profiles | — | ✓ |
| [Favorites](#favorites-full-edition) | — | ✓ |
| [Fine frequency stepping, Radio bands](#fine-frequency-stepping-and-radio-bands-full-edition) | — | ✓ |
| [Checksum structure hypotheses](#unknown-protocol-analysis) | — | ✓ |
| [Waterfall](#waterfall-full-edition) | — | ✓ |
| [Bitstream Explorer](#bitstream-explorer-full-edition) | — | ✓ |
| [Multi-Capture Compare](#multi-capture-compare-full-edition) | — | ✓ |
| [Research Sessions](#research-sessions-full-edition) | — | ✓ |
| Main menu | the tools listed directly | grouped: Scan, Receive & Record, Analyze, Database, Sessions, Settings, About |
| [Replay](#authorized-signal-replay) | the app checks the firmware's region first and explains a refusal; then the firmware's own check | the firmware's own check only |
| Resident code (from the built files) | 77,173 B | 104,388 B, plus [modules](#modules-full-edition) loaded only while used |

The Catalog edition keeps every receiving and analysis feature the app had;
the Full-only features are the research tools that go beyond the built-in
frequency list. A build that defines neither edition is a Catalog build.

**Shared data.** Both editions use `/ext/apps_data/radiogeddon`: `signals/`,
`reports/`, `scans/`, `settings.txt`, and in the Full edition
`favorites.txt` and `profiles/`. Both read and write every settings field,
so settings only the Full edition uses survive the Catalog edition saving
them; a beta 5 settings file loads in both (format tests). Neither edition
deletes, renames or migrates files the other wrote. The two can be installed
side by side (different app ids).

## Verification status at a glance

**Every feature below is implemented, compiles for its editions and firmware
families, and passes CI. None has yet been verified on a physical Flipper
Zero.** "Unit-tested" means the feature's firmware-independent logic is covered
by the host test suite (1,962 checks, plus format, decoder, capture, engine
lifecycle and offline UI tests); radio behaviour and the screens' real look
can only be confirmed on a device ([VERIFICATION.md](VERIFICATION.md)).

| Feature | Implemented | Unit-tested logic | Verified on hardware |
|---------|:-----------:|:-----------------:|:--------------------:|
| [Sub-GHz Scanner](#sub-ghz-scanner) | ✅ | ✅ floor, activity, CSV rows; engine start/stop cycles | ⏳ pending |
| [Range Scanner](#range-scanner-full-edition) *(Full)* | ✅ | ✅ band probe, plans across gaps, display maths; engine start/stop cycles | ⏳ pending |
| [Waterfall](#waterfall-full-edition) *(Full)* | ✅ | ✅ history wraparound, column/pixel mapping, levels, missing cells, render; engine with history on real threads; screen on a host canvas | ⏳ pending |
| [Bitstream Explorer](#bitstream-explorer-full-edition) *(Full)* | ✅ | ✅ bits, bytes from any offset, diff markers, fields; screen and keys on a host canvas | ⏳ pending |
| [Multi-Capture Compare](#multi-capture-compare-full-edition) *(Full)* | ✅ | ✅ synthetic multi-capture sets with known field differences | ⏳ pending |
| [Research Sessions](#research-sessions-full-edition) *(Full)* | ✅ | ✅ file format, a failure at each save step, recovery, grouping; fuzzed | ⏳ pending |
| [Modules](#modules-full-edition) *(Full)* | ✅ | ✅ size estimate; every module's imports checked against each SDK | ⏳ pending |
| [Favorites](#favorites-full-edition) and scan profiles *(Full)* | ✅ | ✅ file format, validation, sorting, failed saves | ⏳ pending |
| [Fine stepping, Radio bands](#fine-frequency-stepping-and-radio-bands-full-edition) *(Full)* | ✅ | ✅ stepping across gaps, band probe | ⏳ pending |
| [Frequency Hopper](#frequency-hopper) | ✅ | ✅ dwell, hold, lock, history; engine start/stop with auto-record | ⏳ pending |
| [RAW Signal Capture](#raw-signal-capture) | ✅ | ✅ ring, file format, writer thread with slow/failing card | ⏳ pending |
| [Protocol Identification](#protocol-identification) | ✅ | — (firmware decoders) | ⏳ pending |
| [Decode with Firmware](#decode-with-firmware) | ✅ | ✅ sample feeding, timing, repeat list; the firmware's decoders on its own test captures | ⏳ pending |
| [Signal Analyzer](#signal-analyzer) | ✅ | ✅ parsing, clustering | ⏳ pending |
| [Unknown Protocol Analysis](#unknown-protocol-analysis) | ✅ | ✅ PWM/PPM/Manchester, noise, alignment, streaming; scored on the firmware's 50 test captures | ⏳ pending |
| [Pulse Timeline](#pulse-timeline) | ✅ | ✅ layout, pan/zoom, frame navigation | ⏳ pending |
| [Signal Comparison](#signal-comparison) | ✅ | ✅ RAW similarity, pattern alignment | ⏳ pending |
| [Device ID Candidate Detection](#device-id-candidate-detection) | ✅ | ✅ constant/changing fields | ⏳ pending |
| [Rolling Code Classification](#rolling-code-classification) | ✅ | ✅ field-map logic | ⏳ pending |
| [Cryptographic Structure Heuristics](#cryptographic-structure-heuristics) | ✅ | ✅ key-byte statistics | ⏳ pending |
| [Signal Database](#signal-database) | ✅ | ✅ index, duplicates, sort/filter/search | ⏳ pending |
| [Authorized Signal Replay](#authorized-signal-replay) | ✅ | ✅ transmit check in both editions | ⏳ pending |
| [Memory diagnostics](#memory-diagnostics) | ✅ | ✅ bookkeeping, Database and recorder lifecycles | ⏳ pending |

## Range Scanner (Full edition)

The Scanner's sequential RSSI sweep over a range instead of a list:

- **Start, end and step** (1 kHz to 10 MHz), **dwell** (1 to 100 ms),
  threshold, **pause on hit** and modulation; start and end are typed in kHz.
- **Bands, not a table.** When the screen opens it asks the radio's driver
  which frequencies it accepts (`subghz_devices_is_frequency_valid`, edges
  found to the hertz by binary search) and lays the start / end / step grid
  over those bands only. On RogueMaster, Momentum and Unleashed that is
  281–361, 378–481 and 749–962 MHz on the internal radio; on Official
  300–348, 387–464 and 779–928 MHz. A range across a gap simply has no points
  there, and the radio is never asked for a frequency it rejects.
- **Limits checked up front.** At most 256 points; the setup shows the point
  count and an estimated sweep time (points × (dwell + 4 ms)), or why the
  range cannot be scanned (`Too many points`, `No tunable points`). The engine
  and screen take one scan channel per point (about 7 KB for 256 points) and
  are allocated only after checking the largest free block leaves 12 KB
  spare; they are freed when the screen closes, also when leaving for Receive.
- **The screen**: one column per pixel with the latest reading, a dot for the
  peak hold, the threshold over the median noise floor of all points as a
  dotted line, active points marked on top, the cursor's frequency, latest /
  peak dBm and counter, and the last sweep's real duration. Left/Right move
  the cursor (held: faster), Down jumps to the strongest peak, OK opens
  Receive there, long OK opens Receive and starts recording, Up pauses (or
  releases a hold), long Up recalibrates every point's noise floor, long Down
  clears peaks and counters, long Right saves a CSV to `scans/`.
- **Profiles**: *Save profile* stores the range, step, dwell, threshold,
  pause-on-hit and modulation as `profiles/<name>.txt`; *Load* and *Delete*
  list them. Replacing or deleting asks first; a damaged profile is refused
  whole.

Like the Scanner it measures one frequency at a time: a sweep over many
points can miss a burst shorter than the sweep. It is not a wideband
spectrum analyzer.

## Waterfall (Full edition)

**Scan → Waterfall** sweeps the Range Scanner's range (its setup opens on
*Start waterfall*) and keeps each complete sweep as one row of a history:
frequency across in the order of the scan's points (gaps the radio cannot
tune take no width; a dot over the picture marks where a band segment
starts), time down, newest on top. The header says what it is — `RSSI
sweep`: sequential narrowband readings, one frequency at a time, **not** a
wideband or IQ/SDR capture, so a burst on a point the sweep is not visiting
is not seen.

- **Intensity** is an ordered-dither density from 0 to 12 of 16 pixels,
  rising linearly over the chosen **sensitivity** span (6, 10, 20, 30 or
  40 dB) above each column's **noise floor** (compensation on; off: above
  -105 dBm). Readings at or above the Range Scanner's threshold over the
  floor are drawn **solid**: strong signals stand out.
- **Missing measurements** (a sweep interrupted by a recalibration, say) are
  drawn as their own sparse dotted pattern, never filled in. With more points
  than the 128 columns, a column shows the strongest of its points in that
  sweep; with fewer, each point is drawn wider.
- **Peak**: a tick marks the column of the strongest stored reading; the
  footer shows the cursor's frequency, its reading in the top visible row and
  its peak over the history, and the measured sweep time (before the first
  sweep: the estimate, also on the waiting screen).
- **Keys**: Left/Right move the cursor (held: faster), Up/Down scroll four
  sweeps newer / older (the view then stays on those sweeps while new ones
  arrive; the header shows `-N`), OK pauses / resumes. Holding OK opens a
  menu: *Receive here* (the strongest point under the cursor), *Cursor to
  peak*, *Newest sweeps*, *Sensitivity*, *Floor comp* and *Save history CSV*
  (`scans/WF_<date>.csv`: one row per sweep, its age, then each column in
  dBm, empty where not measured).
- **Memory**: one byte per cell plus a timestamp per row, in one buffer of
  at most 8 KB and at least one screen (42 rows), sized from the free heap
  and refused before anything is allocated if it does not fit. It runs the
  Range Scanner's own engine (no second radio session); engine, history and
  screen are freed on exit, also when leaving for Receive (the history then
  starts again).

## Bitstream Explorer (Full edition)

**Database → RAW file → Bitstream Explorer** (or **Analyze → Bitstream
Explorer…**) shows the frames the analyzer infers from a RAW capture (the
same engine as Unknown Protocol Analysis, streamed; only its result is kept)
in five views, cycled with OK:

- **FRAMES**: each frame's number, start time, bit count, pattern letter,
  how many frames share its pattern, and its similarity to the reference
  frame (the first of the most common pattern); noise frames say so.
- **BITS**: 24 bits a line with their positions; Left/Right move the bit
  cursor, the footer gives its position and value. Hold OK to start a field
  there.
- **HEX**: whole bytes from a chosen bit offset (hold OK moves the byte grid
  one bit, 0-7); the bits before the first byte and after the last whole
  byte are shown apart as bits, never padded into a byte. The footer gives
  the selected byte's position, hex, decimal and binary.
- **DIFF**: the frame's bits over markers — `.` the same in every comparable
  frame (clean, same length, not cut off), `X` changes, `?` too few frames
  to tell (fewer than two). No bit value is ever inferred.
- **FIELD**: a bit range — Up/Down move its start, Left/Right its end — with
  its binary, hex and decimal value (decimal up to 48 bits; up to 64 bits in
  hex; longer ranges in binary, marked when cut).

Every bit is a `[HYPOTHESIS]` from the encoding guess (the header shows the
encoding and its confidence).

## Multi-Capture Compare (Full edition)

**Compare several…** on a recording (or **Analyze → Multi-Capture Compare**)
builds a list of up to 8 recordings; *Compare now* analyses them one after
another — one file open at a time, each leaving only a ~200-byte summary — and
reports, with a label on every line:

- `[OBSERVED]` frequency (same within 50 kHz or not), preset, and Te (within
  15 %); each recording's encoding, frame length and repeat count;
- `[HYPOTHESIS]` encoding and frame-length consistency, which recordings send
  identical frames (letters), recordings holding several patterns (several
  buttons in one file), and, for the recordings sharing the most common
  length, the bits that stay **constant** or **change**;
- each run of bits classified: constant (`could be an ID, sync or fixed
  field; not verified as a serial`), **button-like** (short, or one-hot
  values), **counter-like** (small steps one way in the order the recordings
  were chosen) or **no simple rule** (data, rolling or encrypted part);
- `[HEURISTIC]` mean similarity and a **noise check**: when every recording
  repeats its frame, differences are steady; a recording holding its frame
  only once may differ by noise.

Decoded key files compare too (their bits come from the firmware's decoder,
`[CONFIRMED]`). Nothing is decrypted, predicted or verified.

## Research Sessions (Full edition)

**Sessions** keeps named groups of recordings, one small text file each in
`apps_data/radiogeddon/sessions/`. A session only *names* recordings of the
signals folder: the `.sub` files are never changed, moved or deleted, and the
stock Sub-GHz app opens them as before.

- **New**, **Rename**, **Delete session** (asks first; its recordings stay).
- **Recordings**: opens a recording's usual menu (analysis, explorer,
  compare, replay); a recording deleted or renamed outside the app is marked
  `?`, not hidden. **Add recording…** / **Remove recording…**.
- **Collect new captures** makes it the active session (`*` in the list):
  recordings saved from Receive join it.
- **Compare recordings** opens Multi-Capture Compare with the first 8;
  **Export report** writes the list and that comparison to `reports/`.
- **Suggest groups** offers recordings with the same frequency (within
  50 kHz), protocol and frame length, each saved within 30 minutes of the
  previous one; a group becomes a session only when you confirm it.
- **Safe saving**: the new file is written beside the old one and read back,
  then swapped in (old → `.bak`, new → name, backup removed); an interrupted
  save leaves the last complete version, restored on the next load. Renaming
  a recording in the app updates the sessions that name it.

## Modules (Full edition)

The Full edition's optional tools are plugins packed inside the `.fap`
(`fal_embedded`) and unpacked by the firmware into the app's assets folder.
Each is loaded when its screen opens and unloaded when it closes, so its code
takes RAM only then:

| Module | Loaded while |
|--------|--------------|
| `radiogeddon_wf.fal` | the Waterfall is open |
| `radiogeddon_bits.fal` | the Bitstream Explorer is open |
| `radiogeddon_multi.fal` | a comparison or a session export runs |
| `radiogeddon_unknown.fal` | an Unknown Protocol Analysis (or a report) runs |
| `radiogeddon_sessions.fal` | any Sessions screen is open |

Before loading, the app checks the module's code and data (its allocated ELF
sections) plus the work it will do fit the free heap; if not, it says `Not
enough memory` and stays where it was. A missing or mismatched module is
reported as such (reinstall the app). The Catalog edition has no modules.

## Favorites (Full edition)

Up to 24 favorite frequencies in `favorites.txt`, kept sorted, without
duplicates. Add the current receive frequency or type one; open a favorite to
receive there, receive and record at once, or make it the receive frequency;
delete asks first. Settings → *Scan source* and *Hop source* switch the
Scanner and the Hopper from the built-in list to the favorites (those the
radio in use can tune). A favorite another radio cannot tune is kept and
marked `(no tune)`.

## Fine frequency stepping and Radio bands (Full edition)

- **Freq step** in Settings makes Left/Right on `Frequency MHz` step by 1, 5,
  10, 12.5, 25 or 100 kHz or 1 MHz within the radio's bands, jumping straight
  over a gap to the next band's edge; `List` steps through the built-in
  frequencies as before.
- **Radio bands** shows the receive bands the radio in use accepts (measured
  as above), the firmware's region and the region's transmit bands.

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
- Uses the frequency and modulation chosen in **Settings**: one of the 19
  listed, or any frequency typed in kHz that the radio in use can tune.

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

## Decode with Firmware

*Decode with Firmware* in a RAW capture's menu runs the same decoders, with
the same keystore, over the saved recording, so a capture made with RAW
recording (or by the Hopper's auto-record, or by another app) can still be
identified afterwards. Every sample goes to the decoders the way the
firmware's own `subghz decode_raw` command feeds a file. The radio is not
used: nothing is received or transmitted.

Each different decode is listed once as `[CONFIRMED]`, with how many times it
was decoded, when in the recording it was first and last decoded, and the
description Receive shows (for example key, serial and button). Up to 12
different decodes are listed; more are counted. A capture no decoder
recognises says so and points to *Unknown Protocol Analysis*. Nothing is
saved apart from an optional report.

The decoders need as much memory as a receive session, so the same check
applies: if the last measured session no longer fits, the screen says
`Not enough memory` instead of starting. *Save report to SD* includes this
decode for RAW captures when it fits.

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
streaming, all on synthetic signals. The capture tests also score it on the
firmware's own 50 RAW test captures: Te right for 50, encoding family for 43,
frame length within one bit for 35. Every reading's fit is listed, so an
ambiguous capture shows as one; when Manchester's grammar fits but every
pulse pairs with its gap by one rule (always opposite, or always equal), the
pulse-width reading is taken and the report says why. The frame list ends
with how often the main patterns repeat within a press (`[OBSERVED]`). The
Full edition adds checksum-structure hypotheses (XOR, sum, CRC-8, parity)
over the distinct frames, naming the frames tested and how many each kind of
check needs. Details:
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
| Catalog edition: the firmware has no region, its region does not allow the frequency, or the radio hardware's transmit check rejects it | `TX refused` with the reason, for example `Region EU does not allow TX on 315.00 MHz`; the Replay screen says so before Send |
| the firmware's radio driver refuses (`subghz_devices_set_tx`) | `Firmware blocked TX` |
| the file is unreadable or its modulation preset isn't recognised | `Unsupported file` |
| its custom preset is empty or damaged (no end, PA table cut) or holds an address that is not a configuration register, such as a command strobe | `Bad custom preset` |

Regional rules are enforced by the firmware itself
(`subghz_devices_set_tx`), and RadioGeddon does not bypass them. The Catalog
edition checks before that, from the firmware's own answers
(`furi_hal_region_is_provisioned`, `furi_hal_region_is_frequency_allowed`,
`furi_hal_subghz_is_frequency_valid`), and refuses when any is negative or
unknown; there is no region selector. The Full edition adds no regional
restriction of its own. A custom
preset is checked first (`rg_preset_check`) because the firmware loads it
unchecked: a command strobe in it, such as STX, would run before that
region check.

## Memory diagnostics

- **About** shows the free heap now, the largest free block, the free heap at
  app start, the lowest the app saw and the step before it, the app's peak
  use, the lowest since boot, the total heap, and what a receive session took
  ([User Guide](USER_GUIDE.md#about)). All figures come from the firmware's
  heap counters; nothing is estimated.
- **Receive and Hopper** measure what starting the radio took and, the next
  time, refuse with `Not enough memory` when that plus 6 KB is no longer free,
  instead of risking an out-of-memory crash. The measurement is kept per
  firmware. Before the first measurement, a session is refused only when less
  than 16 KB is free (it could only run out of memory there).
- **Optional screens** (Waterfall, Bitstream Explorer, comparisons, Sessions,
  Unknown Protocol Analysis in the Full edition) check what they and their
  module need before allocating anything and explain a refusal.
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
