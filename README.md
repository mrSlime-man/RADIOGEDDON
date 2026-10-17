<div align="center">

<img src="docs/assets/brand/banner.svg" alt="RadioGeddon — standalone Sub-GHz signal analysis for Flipper Zero" width="100%">

# RadioGeddon

**A standalone Sub-GHz radio analysis toolkit for the Flipper Zero.**
Scan, hop, capture, identify, analyse, compare and (where authorized) replay
Sub-GHz signals — entirely on the device, with no computer, phone or network.

[![CI](https://github.com/mrSlime-man/RADIOGEDDON/actions/workflows/ci.yml/badge.svg)](https://github.com/mrSlime-man/RADIOGEDDON/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/mrSlime-man/RADIOGEDDON?sort=semver&label=release)](https://github.com/mrSlime-man/RADIOGEDDON/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-22d3ee.svg)](LICENSE)
[![Platform: Flipper Zero](https://img.shields.io/badge/platform-Flipper%20Zero%20(f7)-a855f7.svg)](https://flipperzero.one)
[![Firmware: Official · RogueMaster · Momentum · Unleashed](https://img.shields.io/badge/firmware-Official%20%C2%B7%20RogueMaster%20%C2%B7%20Momentum%20%C2%B7%20Unleashed-0ea5e9.svg)](docs/FIRMWARE_COMPATIBILITY.md)
[![Hardware: partially tested](https://img.shields.io/badge/hardware-partially%20tested-f59e0b.svg)](docs/VERIFICATION.md)

[Download](#-download) · [Install](docs/INSTALLATION.md) · [User Guide](docs/USER_GUIDE.md) · [Features](docs/FEATURES.md) · [Docs](docs/)

</div>

---

> [!NOTE]
> **Stable release 1.0.0.** Every feature is implemented and the builds pass
> all automated checks (lint, 2,903 host-test checks, format, decoder,
> capture, engine-lifecycle, file, memory-stress and offline UI tests,
> ThreadSanitizer, fuzzing, static analysis of both editions, five firmware
> builds with API, manifest, imported-symbol and module verification).
> **Hardware testing is real but limited:** the owner ran the 1.0.0 code
> (released as beta 13) on a Flipper Zero with RogueMaster `4f2e06200`
> (API 88.4) — four launches and exits without a reboot, the Frequency
> Scanner, the Adaptive Scanner and the RF Activity Logger after it, no
> crash, about 45 KB of heap free after start. Receiving, recording,
> transmitting, most of the 19 modules and the Catalog edition on Official
> firmware are **not** yet confirmed on a device. See
> [VERIFICATION.md](docs/VERIFICATION.md) for exactly what has and hasn't
> been tested; reports from the [hardware checklist](docs/HARDWARE_CHECKLIST.md)
> are the most useful contribution.

## What is it?

RadioGeddon turns a Flipper Zero into a self-contained Sub-GHz workbench. It
reuses the firmware's own radio drivers and protocol decoders — so
identification matches the stock Sub-GHz app — and adds a layer of analysis on
top: per-frequency signal-strength scanning, a frequency hopper, RAW capture,
pulse-timing and line-encoding analysis for unknown protocols, constant-vs-
changing field maps across repeated presses, side-by-side comparison, a
static-vs-rolling classifier, an on-SD-card signal database, and authorized
replay. Everything runs on the device; recordings are plain `.sub` files.

It is an **analysis and authorized-testing** tool. RadioGeddon itself does
**not** recover keys, decrypt payloads, or predict rolling codes, and it refuses
to replay decoded rolling-code protocols — those are deliberate non-goals. (It
does display the firmware's own decoder output, which may use the SD-card
keystore to identify KeeLoq-family signals.)

## Two editions

RadioGeddon comes in two editions built from one source tree
([`radiogeddon_edition.h`](radiogeddon_edition.h)):

- **RadioGeddon Full** — the flagship research edition for **RogueMaster**,
  **Momentum** and **Unleashed**. Everything below, plus range scanning across
  every band the radio tunes, an RSSI sweep-history **Waterfall**, the
  **Bitstream Explorer**, **Multi-Capture Compare**, research **Sessions**,
  scan profiles, favorite frequencies, fine frequency stepping and
  checksum-structure hypotheses, and the **Dolphin Lab** interface. Its
  optional tools load from the SD card only while their screen is open.
  Transmitting follows the installed firmware's own rules.
- **RadioGeddon Catalog** — the **Official firmware** edition, prepared for the
  Flipper Apps Catalog. The full receive and analysis toolkit; before any
  transmission it asks the firmware for its region and refuses (and says why)
  unless that region allows the frequency.

| | Catalog | Full |
|---|:---:|:---:|
| Target firmware | Official | RogueMaster, Momentum, Unleashed |
| Scanner (19-frequency list), Frequency Hopper | ✓ | ✓ |
| Receive & decode, streaming RAW recording | ✓ | ✓ |
| Decode with Firmware, Signal Analyzer, Unknown Protocol Analysis | ✓ | ✓ |
| Pulse Timeline, comparison, Signal Database | ✓ | ✓ |
| External CC1101, memory diagnostics | ✓ | ✓ |
| Replay | region-checked by the app **and** the firmware | the firmware's own rules |
| **Range Scanner** (start/end/step, 256 points, crosses band gaps) | — | ✓ |
| **Scan profiles** (save / load / delete) | — | ✓ |
| **Favorites** (Receive, Scanner and Hopper sources) | — | ✓ |
| **Fine frequency stepping** (1 kHz – 1 MHz across gaps), **Radio bands** screen | — | ✓ |
| **Checksum / CRC structure hypotheses** | — | ✓ |
| **Waterfall** (RSSI sweep history of the range) | — | ✓ |
| **Bitstream Explorer** (frames, bits, hex, diff, fields) | — | ✓ |
| **Multi-Capture Compare** (up to 8 recordings) | — | ✓ |
| **Research Sessions** (groups of recordings, grouping suggestions) | — | ✓ |
| **Dolphin Lab** (animated mascot, dashboard, Cyber Lab / Classic styles, lab room) | Classic menus | ✓ |
| **Find Similar Signals** (signal fingerprints, explained matches) | — | ✓ |
| **Pulse Studio** (waveform, cursor, marker, bits, hex, diff, timing) | — | ✓ |
| **Smart Protocol Discovery** (competing readings with evidence) | improved analyzer | ✓ |
| **Adaptive Scanner**, **RF Activity Logger** | — | ✓ |
| **Research Journal**, optional Research XP, **Dolphin Assistant**, customization | — | ✓ |
| App name / appid | RadioGeddon / `radiogeddon` | RadioGeddon Full / `radiogeddon_full` |

Both editions keep their data in `/ext/apps_data/radiogeddon`, so recordings,
settings, favorites and profiles carry over when you switch — see
[Features → Editions](docs/FEATURES.md#editions).

## 🔬 Research Lab (beta 9)

The Full edition is a standalone research lab: everything runs on the
Flipper, with no computer, phone, network or cloud service.

- **Find Similar Signals** — each RAW recording gets a 64-byte fingerprint
  (timing ratios, pulse and gap histograms, preamble, sync, frames, repeats,
  bit length, encoding). *Find Similar* ranks your recordings against one and
  says why: what agrees and what contradicts. Thin evidence caps the score.
- **Pulse Studio** — a RAW capture on one screen: waveform with zoom, cursor
  and measuring marker, preamble/glitch/frame-end marks, the decoded bit under
  each symbol (`?` where unsure), and Frame, Bits, Hex, Diff and Timing pages.
- **Smart Protocol Discovery** — competing readings, each with its evidence
  and score, instead of a single answer; on the firmware's 50 test captures
  the encoding family is right for 48 (beta 8: 43) and the frame length for 43
  (beta 8: 35), 44 against the lengths the decoders actually decode. No
  protocol answer is hardcoded.
- **Adaptive Scanner** — Balanced Adaptive, Activity Focused and Custom modes
  visit busy frequencies more while every frequency keeps a bounded wait.
- **RF Activity Logger** — hours of passive observation to CSV, keeping
  "quiet" and "not observed" apart, with a summary; it never overwrites.
- **RF Activity Heatmap** — a saved log as frequency × time on the screen:
  not observed (blank), observed and quiet (a dot), activity by bursts or
  strength, with each cell's coverage.
- **Recording recovery and crash-safe files** (both editions) — a capture
  cut off by a reboot or a flat battery is repaired and offered back at the
  next start; settings, the journal, favorites, profiles and the fingerprint
  index survive a reboot in the middle of a save.
- **Diagnostics and Battery saver** (both editions) — About shows
  responsiveness, module, recording and heap figures and saves them as a
  report; the battery saver redraws live screens less often without slowing
  the radio.
- **Research Journal**, optional **Research XP** and 12 achievements (real
  events only, nothing for transmitting), a rule-based **Dolphin Assistant**
  (one dismissable tip at a time, never while recording), dolphin
  customization, *Animation level* Off / Minimal / Normal / Full, and a lab
  room with six stations.

Nothing in beta 9 or 10 transmits; RadioGeddon does not recover keys, predict
rolling codes or brute-force anything.

<img src="docs/assets/beta9-previews.png" alt="Synthetic previews: Pulse Studio's waveform and timing pages, the Adaptive Scanner, the lab room's workbench, equipment rack, dolphin corner and research terminal, and Pulse Studio's frame page" width="100%">

<sub>Synthetic development previews drawn by the host UI tests (`make -C test ui`)
with the firmware's own fonts — not screenshots of a Flipper Zero.</sub>

## 🐬 Dolphin Lab

The Full edition opens on **Dolphin Lab**: a small radio laboratory with an
original animated dolphin engineer (headphones, antenna, lab goggles) beside
the main menu. The dolphin reacts to what the radio really does — it watches
the radar while a scan runs, jumps at a detection, listens while receiving,
runs the recorder (REC shows only while the recorder does), works at the
terminal during an analysis (a percentage only when the work reports one),
celebrates a save, worries at an error or low memory, and dozes off when
nothing happens for a while. A small badge in the radio screens' headers
follows the same state.

- **Three styles** (Settings > UI style): *Dolphin Lab*, the technical
  *Cyber Lab*, and *Classic* — the Beta 7 menus with no mascot and no
  animation. *Transitions* and optional *UI feedback* (Off / Minimal / Full)
  are settings too, and none of them changes what the radio does.
- **Dolphin Lab room** (main menu > Dolphin Lab): a workbench with the
  recording you are working on, an oscilloscope that shows its first pulses,
  a radar, a research terminal with your journal, an equipment rack that
  opens the real tools, and the dolphin's corner (every pose, customization).
  It never uses the radio.
- Animation runs on the app's existing 100 ms tick, redraws only when the
  picture changes, never delays a key, and stops when the screen is not
  shown. The room's code is a module loaded only while it is open.

<img src="docs/assets/dolphin-lab-previews.png" alt="Synthetic previews of the Dolphin Lab dashboard (idle, scanning, recording), Cyber Lab, a save card, an analysis progress card, the lab room's terminal and the Scanner's header badge" width="100%">

<sub>Synthetic development previews drawn by the host UI tests (`make -C test ui`)
with the firmware's own fonts — not screenshots of a Flipper Zero.</sub>

## ⬇ Download

Grab the build that matches your firmware from the
[**v1.0.0 release**](https://github.com/mrSlime-man/RADIOGEDDON/releases/tag/v1.0.0),
then copy it to `apps/Sub-GHz/` on the SD card (full steps in
[Installation](docs/INSTALLATION.md)).

| Your firmware | Edition | Download this file |
|---------------|---------|--------------------|
| **Official** (flipperzero.one) | Catalog | [`radiogeddon-catalog-official.fap`](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0/radiogeddon-catalog-official.fap) |
| **RogueMaster** | Full | [`radiogeddon-full-roguemaster.fap`](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0/radiogeddon-full-roguemaster.fap) |
| **Momentum** | Full | [`radiogeddon-full-momentum.fap`](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0/radiogeddon-full-momentum.fap) |
| **Unleashed** | Full | [`radiogeddon-full-unleashed.fap`](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0/radiogeddon-full-unleashed.fap) |
| **RogueMaster `4f2e0620 [88.4]`** (exact device build) | Full | [`radiogeddon-full-roguemaster-4f2e06200-api88.4.fap`](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0/radiogeddon-full-roguemaster-4f2e06200-api88.4.fap) |

The last file is a device-specific build for RogueMaster commit `4f2e06200`
(API 88.4), made in that commit's own tree. If *Settings → About → Firmware
Info* shows `4f2e0620 [88.4]`, take it; otherwise take the RogueMaster file
above. Install only one of the two.

Each firmware family has its own SDK and symbol table, so **install the file
for your firmware** — the wrong one is refused ("Outdated App / Firmware" or
a missing-symbol error) rather than loading. Every file is compiled against
that firmware's own pinned SDK; none is a renamed copy of another. Not sure
which you have? See [Firmware Compatibility](docs/FIRMWARE_COMPATIBILITY.md).
Every release lists `SHA256SUMS` and ships signed build-provenance
attestations. The Catalog edition is being submitted to the official Apps
Catalog; it is **not** listed there yet, so install it from the release page.
Earlier beta release notes stay in [docs/releases](docs/releases/).

## Features

| Module | What it does |
|--------|--------------|
| **Sub-GHz Scanner** | Narrowband RSSI sweep over a configurable list of 19 common frequencies (Full: or your favorites): noise floor, activity detection, peak hold, burst counts, CSV export; OK receives there, long OK receives and starts recording. |
| **Range Scanner** *(Full)* | Start / end / step sweep of up to 256 points across every band the radio accepts, skipping the gaps; dwell, threshold, pause on hit, noise-floor recalibration, peak hold, counters, sweep-time estimate, CSV export and saved profiles. |
| **Waterfall** *(Full)* | The Range Scanner's sweeps as an **RSSI sweep history**: frequency across, one row per complete sweep, newest on top, signal strength as dither density over each column's noise floor, strong signals solid, unmeasured cells marked. Pause, scroll back, sensitivity, peak, Receive on the cursor and CSV export. Sequential narrowband sweeps — not a wideband or SDR capture. |
| **Favorites** *(Full)* | Up to 24 favorite frequencies; receive or record on one, or scan / hop the whole list. |
| **Frequency Hopper** | Hops a configurable list (default 315 / 390 / 433.92 / 868.35 MHz), holds on noise-floor-relative activity or decodes, lock/next, statistics, optional auto RAW recording. |
| **RAW Signal Capture** | Streams the raw on/off timing to a standard RAW `.sub` file on the SD card while recording, so length is limited by the card, not RAM; shows time, samples and any samples lost to a slow card. |
| **Protocol Identification** | Live decoding with the firmware's own decoders (Princeton, CAME, Nice FLO, Holtek, KeeLoq-family, …) — marked `[CONFIRMED]`. |
| **Signal Analyzer** | Pulse-width groups and base time unit for RAW; bit/field breakdown for decoded protocols. |
| **Unknown Protocol Analysis** (Full: **Smart Protocol Discovery**) | Streams a whole RAW capture: measured timing, noise and frames (`[OBSERVED]`), then the encoding (PWM/PPM/Manchester) with a confidence score, competing readings with their evidence, bit patterns, frame-by-frame comparison and field map (`[HYPOTHESIS]`). Full: also tests whether independent frames end in a common checksum (XOR, sum, CRC-8, parity). |
| **Find Similar Signals** *(Full)* | Ranks your recordings by signal fingerprint against one, with the features that agree and contradict; open or compare any match. |
| **Pulse Studio** *(Full)* | One RAW capture's frames: waveform with zoom, cursor and marker, decoded bits under the symbols, Frame, Bits, Hex, Diff and Timing pages. |
| **Adaptive Scanner** *(Full)* | Standard, Balanced Adaptive, Activity Focused or Custom scheduling of the Scanner's list, with each frequency's share and the longest wait shown. |
| **RF Activity Logger** *(Full)* | Passive observation for up to 8 h (or until stopped): per-interval CSV rows, size-limited parts, a summary of the busiest frequencies and coverage. |
| **Research Journal** *(Full)* | Real research events, optional XP, levels and achievements, and the Dolphin Assistant's tips. |
| **Pulse Timeline** | Zoomable, scrollable waveform of a RAW capture with frame markers and pulse durations, streamed from the SD card. |
| **Bitstream Explorer** *(Full)* | The bits the analyzer infers from a RAW capture, frame by frame: binary with positions, whole bytes from any bit offset (leading and trailing bits shown apart, never padded), which bits stay constant or change across comparable frames (`?` where the evidence is too thin), and any bit range's binary, hex and decimal value. |
| **Multi-Capture Compare** *(Full)* | Up to 8 recordings analysed one after another: frequency, preset, timing, encoding and length consistency, identical frames, similarity, and the bits that stay constant or change — reported as constant, button-like, counter-like or no simple rule, with an `[OBSERVED]`/`[HEURISTIC]`/`[HYPOTHESIS]` label on every line. |
| **Research Sessions** *(Full)* | Named groups of recordings in small text files: add, remove, open, compare and export; the active session collects new captures; grouping suggestions by frequency, protocol, length and time — confirmed by you, never automatic. Recordings are never modified or deleted by a session. |
| **Signal Comparison** | Field-by-field diff of two recordings; for RAW captures, a timing-similarity score and a frame-pattern comparison that does not depend on when recording started. |
| **Device ID Candidate Detection** | Offers the longest run of bits that stay constant while others change as a candidate device identifier. |
| **Rolling Code Classification** | Flags static vs. dynamic (rolling-code) protocols, and highlights changing bits in unknown ones. |
| **Cryptographic Structure Heuristics** | Key-byte variety and key-delta hints — never key recovery. |
| **Signal Database** | Browse, analyse, compare, replay and delete recordings stored as `.sub` on the SD card. |
| **Authorized Signal Replay** | Transmits RAW and static-protocol captures; refuses decoded rolling-code protocols and unrecognised modulations. A RAW capture is sent exactly as recorded. Catalog: refused, with the reason on screen, unless the firmware's region allows the frequency. |

Analysis conclusions are labelled **`[CONFIRMED]`** (a firmware decoder
matched), **`[OBSERVED]`** (measured from the timing), **`[HEURISTIC]`** (a
statistics-based guess) or **`[HYPOTHESIS]`** (an engine inference) — a guess is never dressed up as a decode (plain summary and
field lines carry no label). Full details, including
verification state per feature, are in [Features](docs/FEATURES.md) and
[Protocol Analysis](docs/PROTOCOL_ANALYSIS.md).

## Install

1. Download the `.fap` for your firmware (above).
2. Copy it to `SD Card/apps/Sub-GHz/` with qFlipper or a card reader.
3. On the Flipper: **Apps → Sub-GHz → RadioGeddon**.

Step-by-step instructions, download verification and troubleshooting:
[Installation](docs/INSTALLATION.md) · [Troubleshooting](docs/TROUBLESHOOTING.md).

## Usage

Physical buttons only: **Up/Down** move, **OK** selects, **Left/Right** change
settings (and **Left** toggles RAW recording while receiving), **Back** returns.

The Full edition's main menu groups the tools: **Scan** (Frequency Scanner,
Adaptive Scanner, RF Activity Logger, Range Scanner, Waterfall, Frequency
Hopper, Favorites), **Receive & Record**, **Analyze** (the current recording,
open a recording, Pulse Studio, Find Similar, Bitstream Explorer,
Multi-Capture Compare, Protocol Discovery, Decode with Firmware, Pulse
Timeline, Research Journal), **Database**,
**Sessions**, **Settings**, **About**. The Catalog edition's menu is unchanged.

A typical session: open the **Scanner** to find a device's frequency → press
**OK** to receive there → watch it decode, or press **Left** to RAW-record →
**OK**/save → open it from the **Database** to analyse, compare, or (where you
are authorized) replay. The full walkthrough of every screen is in the
[User Guide](docs/USER_GUIDE.md).

## Architecture

```mermaid
flowchart LR
    CC1101["CC1101 radio"] <--> DEV["subghz_devices<br/>(portable radio API)"]
    DEV --> RADIO["radiogeddon_subghz<br/>RX · RAW capture · hop · TX gate"]
    RADIO --> DEC["firmware protocol<br/>decoders → [CONFIRMED]"]
    RADIO --> ENG["rg_analyzer + dsp<br/>pure engine → [HYPOTHESIS]"]
    RADIO --> STORE[("/ext/apps_data/<br/>radiogeddon/signals<br/>.sub files")]
    STORE --> ENG
    UI["scenes + views<br/>(ViewDispatcher + SceneManager)"] --> RADIO
    UI --> STORE
```

The radio layer uses only the portable `subghz_devices` API — the same one the
stock app uses — so a single source tree builds both editions for all four
firmware families. The Full edition's optional tools (the Range Scanner and
Waterfall, Settings, the Frequency Hopper, the Bitstream Explorer,
Multi-Capture Compare, Unknown Protocol Analysis, Sessions, the lab room and
journal, Find Similar, Pulse Studio and the Pulse Timeline, the activity
logger, adaptive setup and heatmap, analysis reports, About, recording
recovery and, since beta 13, the Signal Database, the analysis menu and the
Frequency Scanner's screen — 19 modules) are
plugins packed inside the `.fap` and loaded only while their screen is open,
so their code takes RAM only then. Beta 11 works on the memory the app
uses while it runs: the Bitstream Explorer holds 4,072 B instead of 8,672 B,
the Database's entries are 28 bytes instead of 40, Find Similar tracks a
recording in 4 bytes and matches names exactly, and each module's load
cost is read from its own file ([Performance](docs/PERFORMANCE.md);
simulated and computed, not measured on a device). The analysis engine (`helpers/rg_analyzer.*`, `helpers/radiogeddon_dsp.*`)
has no firmware dependencies, so it is unit-tested on a normal computer. Deeper
dive: [Architecture](docs/ARCHITECTURE.md).

## Building from source

```bash
git clone https://github.com/mrSlime-man/RADIOGEDDON.git
cd RADIOGEDDON
source scripts/firmware_pins.sh && pip install "ufbt==${UFBT_VERSION}"

scripts/build_target.sh catalog-official   # -> dist/release/radiogeddon-catalog-official.fap (+ verified)
scripts/build_target.sh full-momentum
scripts/build_target.sh full-unleashed
scripts/build_target.sh full-roguemaster   # clones RogueMaster at a pinned commit (large)
scripts/build_release.sh                   # all four + SHA256SUMS + BUILD_INFO.txt
```

Each build writes the edition's copy of the source
([`scripts/stage_edition.py`](scripts/stage_edition.py)), downloads the exact
SDK pinned in [`scripts/firmware_pins.sh`](scripts/firmware_pins.sh), verifies
its SHA-256 and API version, compiles, and checks the resulting `.fap`'s
manifest and that every symbol it imports is exported by that SDK. A plain
`ufbt` in the checkout builds the Catalog edition, as the Apps Catalog does. More in
[Contributing](CONTRIBUTING.md#development-setup).

## Testing & verification

```bash
make -C test check               # 2,903 host checks (both editions) and the fuzz corpus, ASan + UBSan
make -C test formats             # the firmware's file code, its 85 Sub-GHz test files, settings, favorites, profiles
make -C test decoders            # the firmware's Sub-GHz decoders on its RAW test captures, and its presets
make -C test captures            # the analyzer on those captures, scored against the decoders
make -C test lifecycle           # Scanner, Range Scanner, Waterfall, Hopper started/stopped repeatedly, allocations counted
make -C test ui                  # screens drawn on a host 128x64 canvas with the firmware's fonts (synthetic previews)
make -C test fpindex             # the fingerprint index through the firmware's FlipperFormat on a stub SD card
make -C test actlogfiles         # activity log files: rotation, summary, a full card
make -C test memstress           # heavy operations on a model of the firmware's heap: low memory, leaks, fragmentation
make -C test benchmem            # their peak and held heap, largest free block, fragments (docs/PERFORMANCE.md)
python3 scripts/check_catalog.py # the Catalog edition against the Apps Catalog's rules
python3 scripts/check_links.py   # documentation links and anchors
```

CI runs the link check, the release metadata check, the host, format,
decoder, capture, lifecycle, memory-stress and offline UI tests, the module
load-cost check and load replay, the fuzzers, the Apps Catalog checks
(including the catalog's own bundler), static analysis of both editions (GCC
`-fanalyzer` and clang-tidy) and all four firmware builds (with API, manifest
and imported-symbol verification plus lint) on every pull request and release. A weekly firmware watch builds the app
against newer firmware SDKs as they appear.
**CI cannot exercise the radio**, so on-device behaviour is only as verified
as the hardware reports behind it — tracked honestly in [VERIFICATION.md](docs/VERIFICATION.md) with the test plan
in [HARDWARE_CHECKLIST.md](docs/HARDWARE_CHECKLIST.md).

## Roadmap

RadioGeddon 1.0.0 is the first stable release, and the project is now in
maintenance: bug fixes from reports, firmware compatibility updates, and
optional feature updates when they are worth it. No further beta series is
planned. See the [Roadmap](docs/ROADMAP.md).

## Contributing

Contributions are welcome — especially **hardware test reports**. See
[CONTRIBUTING.md](CONTRIBUTING.md), pick up an
[issue](https://github.com/mrSlime-man/RADIOGEDDON/issues), and note the
[Code of Conduct](CODE_OF_CONDUCT.md). Found a security problem? Report it
privately per the [Security Policy](SECURITY.md).

## License & responsible use

RadioGeddon is released under the [MIT License](LICENSE).

It is intended for education, research, and testing of devices you own or are
explicitly authorized to test. Receiving and especially transmitting radio
signals is regulated and the rules vary by country; the firmware's regional
restrictions stay in force, but **you** are responsible for complying with the
law where you are. RadioGeddon itself does not break encryption, recover keys,
or defeat rolling codes.

<div align="center">
<sub>Built for the Flipper Zero community. Not affiliated with Flipper Devices Inc.</sub>
</div>
