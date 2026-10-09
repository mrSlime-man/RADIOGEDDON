# Features

The complete list of what RadioGeddon does, how far each feature has been
verified, and its known limits. For step-by-step use, see the
[User Guide](USER_GUIDE.md).

## Verification status at a glance

**Every feature below is implemented, compiles for all three firmware
families, and passes CI. None has yet been verified on a physical Flipper
Zero.** "Unit-tested" means the feature's firmware-independent logic is covered
by the host test suite (55 checks); radio behaviour can only be confirmed on a
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

Sweeps 19 common frequencies and shows live received signal strength (RSSI)
for each as a bar and a dBm value, so you can see which frequency a device is
using. Select a frequency to jump straight into *Receive & Record* on it.

- Frequencies (MHz): 300.000, 303.875, 304.250, 310.000, 315.000, 318.000,
  390.000, 418.000, 433.075, 433.420, 433.920, 434.420, 434.775, 438.900,
  464.000, 779.000, 868.350, 915.000, 925.000.
- One frequency is measured every 100 ms (about 2 s per full pass); readings
  are snapshots, not peak-hold, so very short bursts can be missed.
- Receive-only.

## Frequency Hopper

Continuous receive across **315.00, 390.00, 433.92 and 868.35 MHz**. It listens
about 200 ms per band and, when RSSI reaches −90 dBm or more, holds that band
for about two seconds so a decode can complete. Decoded signals are listed and
saved exactly as in *Receive & Record*. Hopping retunes the running receiver
without powering the radio down, using the same stop/retune/start sequence as
the firmware's own hopper.

## RAW Signal Capture

Records the raw on/off timing stream of a transmission — useful for protocols
the firmware can't decode — and saves it as a standard RAW `.sub` file
(`Filetype: Flipper SubGhz RAW File`), readable by the stock Sub-GHz app.

- Up to **16,384** timing samples per recording, buffered in RAM and written to
  the SD card when you save; the screen shows `FULL` when the limit is reached.
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
- **Settings are not persistent** — frequency and modulation reset to 433.92 MHz /
  AM 650 when the app starts.
- **RAW capture length** — 16,384 samples per recording; engine-based analysis
  and similarity use the first 4,096 samples of a file.
- **Internal radio only** — the app uses the built-in CC1101.
- **Scanner and hopper sampling** — measurements are periodic snapshots, so brief
  transmissions can be missed.
- **Firmware coupling** — each `.fap` only loads on firmware with the matching
  API major version ([Firmware Compatibility](FIRMWARE_COMPATIBILITY.md)).
