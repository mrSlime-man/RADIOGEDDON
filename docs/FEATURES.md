# Features

The complete list of what RadioGeddon does, how far each feature has been
verified, and its known limits. For step-by-step use, see the
[User Guide](USER_GUIDE.md).

## Verification status at a glance

**Every feature below is implemented, compiles for all three firmware
families, and passes CI. None has yet been verified on a physical Flipper
Zero.** "Unit-tested" means the feature's firmware-independent logic is covered
by the host test suite (128 checks); radio behaviour can only be confirmed on a
device ([VERIFICATION.md](VERIFICATION.md)).

| Feature | Implemented | Unit-tested logic | Verified on hardware |
|---------|:-----------:|:-----------------:|:--------------------:|
| [Sub-GHz Scanner](#sub-ghz-scanner) | ✅ | — | ⏳ pending |
| [Frequency Hopper](#frequency-hopper) | ✅ | — | ⏳ pending |
| [RAW Signal Capture](#raw-signal-capture) | ✅ | ✅ RAW file round-trip | ⏳ pending |
| [Protocol Identification](#protocol-identification) | ✅ | — (firmware decoders) | ⏳ pending |
| [Signal Analyzer](#signal-analyzer) | ✅ | ✅ parsing, clustering | ⏳ pending |
| [Unknown Protocol Analysis](#unknown-protocol-analysis) | ✅ | ✅ PWM, PPM shape, framing, bits | ⏳ pending |
| [Signal Comparison](#signal-comparison) | ✅ | ✅ RAW similarity | ⏳ pending |
| [Device ID Candidate Detection](#device-id-candidate-detection) | ✅ | ✅ constant/changing fields | ⏳ pending |
| [Rolling Code Classification](#rolling-code-classification) | ✅ | ✅ field-map logic | ⏳ pending |
| [Cryptographic Structure Heuristics](#cryptographic-structure-heuristics) | ✅ | ✅ key-byte statistics | ⏳ pending |
| [Signal Database](#signal-database) | ✅ | — | ⏳ pending |
| [Authorized Signal Replay](#authorized-signal-replay) | ✅ | — | ⏳ pending |

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

- Up to **16,384** timing samples per recording, buffered in RAM and written to
  the SD card when you save; the screen shows `FULL` when the limit is reached.
  The buffer is allocated when recording starts and sized to the free heap, so
  it can be smaller on a busy device; it is released once the capture is saved.
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

Runs RadioGeddon's signal engine over a RAW capture (first 4,096 samples) and
reports, all as `[HYPOTHESIS]`:

- base Te and timing groups;
- a line-encoding hypothesis — PWM/OOK, PPM (gap-coded) or Manchester — with a
  confidence percentage;
- frame count and repeated frames;
- the extracted bit string (PWM only, up to 256 bits);
- a constant-vs-changing field map across repeated frames.

Unit tests cover PWM identification, PPM-shaped input, framing, bit extraction
and field maps on synthetic signals; the Manchester branch has no dedicated
test yet. Details: [Protocol Analysis](PROTOCOL_ANALYSIS.md#unknown-protocol-analysis--hypothesis).

## Signal Comparison

*Compare with…* puts two recordings side by side, marking each field as the
same (`=`) or different (`~`): protocol, frequency, and the key for decoded
protocols — with a `[HEURISTIC]` key-difference hint for same-protocol pairs.
For two RAW captures it adds a **timing-match score (0–100 %)** with a verdict:
near-identical (≥ 90), similar structure (≥ 60), or clearly different.

The score compares captures sample by sample from their start without
alignment, so captures that begin at different points score lower even for the
same button.

## Device ID Candidate Detection

When a RAW capture contains repeated frames (from one or several button
presses), *Unknown Protocol Analysis* packs the bit positions that never change
into a hexadecimal **device ID candidate** (up to 64 bits). Fixed remote or
device identifiers usually live in that constant part of the frame. It is a
`[HYPOTHESIS]` — a candidate to investigate, not a verified serial number.

## Rolling Code Classification

Two complementary signals:

- **Decoded protocols** — *Crypto Analysis* reports the firmware registry's
  classification: `[CONFIRMED] Static code`, `[CONFIRMED] Dynamic code`
  (rolling, KeeLoq-style) or `[CONFIRMED] Telemetry`.
- **Unknown protocols** — *Unknown Protocol Analysis* marks bits that change
  between repeated frames; changing bits alongside a constant block are
  reported as resembling a rolling counter (`[HYPOTHESIS]`). Record several
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
- Browse with the firmware file browser, then analyse, compare, replay or delete;
  deleting asks for confirmation.
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

## Not implemented / out of scope

| Item | Status |
|------|--------|
| On-device verification of all features | Next milestone — [hardware checklist](HARDWARE_CHECKLIST.md) |
| External CC1101 module selection | Planned ([Roadmap](ROADMAP.md)) |
| Custom frequency entry, custom modulation entry | Planned — currently a fixed table and four presets |
| Renaming saved recordings in the app | Planned — rename via qFlipper meanwhile |
| Saving analysis reports to the SD card | Planned |
| Decoders beyond the firmware's own library | Not planned for now — identification relies on the firmware |
| Desktop companion application | Deferred; files stay standard `.sub` so one can be built later |
| Key recovery, rolling-code bypass, brute force, jamming | **Never** — out of scope by design |

## Known limitations

- **Hardware verification pending** — see above.
- **RAW capture length** — 16,384 samples per recording; engine-based analysis
  and similarity use the first 4,096 samples of a file.
- **Internal radio only** — the app uses the built-in CC1101.
- **Scanner and hopper sampling** — the radio hears one frequency at a time, so
  a transmission on a frequency the sweep is not currently on can be missed.
- **Firmware coupling** — each `.fap` only loads on firmware with the matching
  API major version ([Firmware Compatibility](FIRMWARE_COMPATIBILITY.md)).
