# Architecture

RadioGeddon is a single Flipper Zero external application (`.fap`) written in
C. Everything — reception, decoding, storage and analysis — runs on the
device's STM32WB55 and CC1101 radio; nothing is sent to a phone or computer.

This page explains how the pieces fit together. For what each screen does, see
the [User Guide](USER_GUIDE.md); for the analysis logic, see
[Protocol Analysis](PROTOCOL_ANALYSIS.md).

## Overview

```mermaid
flowchart LR
    subgraph HW["Flipper Zero hardware"]
        CC1101["CC1101 radio<br/>(internal, cc1101_int)"]
        SD[("microSD<br/>/ext/apps_data/radiogeddon/signals")]
    end

    subgraph FW["Firmware services (public SDK APIs)"]
        DEV["subghz_devices<br/>portable radio layer"]
        WORKER["SubGhzWorker<br/>(timing → level/duration pairs)"]
        REG["Protocol registry<br/>+ SubGhzReceiver decoders"]
        TXL["SubGhzTransmitter /<br/>file encoder worker"]
        STORE["Storage + FlipperFormat"]
        GUI["ViewDispatcher + SceneManager"]
    end

    subgraph APP["RadioGeddon"]
        RADIO["radiogeddon_subghz<br/>RX · RAW capture · retune · TX gate"]
        HIST["radiogeddon_history<br/>session decodes (32)"]
        STOR["radiogeddon_storage<br/>.sub read/write"]
        ANA["radiogeddon_analysis<br/>describe · analyze · crypto · compare"]
        ENG["rg_analyzer + radiogeddon_dsp<br/>pure signal engine (host-tested)"]
        SCN["scenes/ + views/<br/>14 screens, 2 live views"]
    end

    CC1101 <--> DEV
    DEV --> WORKER --> RADIO
    RADIO --> REG --> RADIO
    RADIO --> HIST
    RADIO --> TXL --> DEV
    STOR <--> STORE <--> SD
    ANA --> STOR
    ANA --> ENG
    SCN --> RADIO
    SCN --> ANA
    SCN --> STOR
    SCN <--> GUI
```

## Source layout

```
radiogeddon.c / .h          App context: allocates services, views and scenes; 100 ms UI tick
radiogeddon_version.h       Release version string (checked against the release tag)
application.fam             Flipper app manifest (appid, category, icon, sources)
scenes/                     One file per screen; the scene list is generated from
                            radiogeddon_scene_config.h (X-macros)
views/                      Custom canvas views: scanner sweep, live receiver
helpers/
  radiogeddon_subghz.*      Radio wrapper: device, decoders, RAW capture, hopper retune, TX
  radiogeddon_storage.*     SD-card layout, .sub parsing, RAW sample loading
  radiogeddon_history.*     Per-session list of decoded signals (max 32, de-duplicated)
  radiogeddon_analysis.*    Text reports: info, analysis, crypto, compare, unknown-protocol
  radiogeddon_dsp.*         Pure RAW parsing / clustering helpers (no firmware headers)
  rg_analyzer.*             Pure signal-analysis engine (no firmware headers)
assets/                     10x10 launcher icon (compiled into the .fap)
test/                       Host unit tests (55 checks) + reference .sub fixtures
scripts/                    Pinned builds, manifest verification, packaging, link check
tools/brand/                Generator for the logo, banner and social preview
.github/workflows/          CI (ci.yml), shared build pipeline (build.yml), release.yml
```

## Screens and navigation

The app uses the firmware's standard GUI framework: a `ViewDispatcher` owns
the views and a `SceneManager` drives navigation. Each scene has
`on_enter` / `on_event` / `on_exit` handlers; the 100 ms tick event drives the
live displays (RSSI, scanner sweep, hopper timing, recording counters).

Reusable firmware widgets (`Submenu`, `Widget`, `VariableItemList`, `Popup`,
`TextInput`, `TextBox`) cover menus and reports. Two custom views draw the live
screens: the **scanner** (per-frequency RSSI bars) and the **receiver** (RSSI
meter, recording status, decoded-signal list), which the Frequency Hopper also
reuses.

## Radio layer

All radio access goes through `helpers/radiogeddon_subghz.c`, which uses only
the portable `subghz_devices_*` API — the same layer the stock Sub-GHz app
uses — so RadioGeddon builds unchanged for Official, Unleashed and RogueMaster.

**Receive.** The CC1101 is configured with a standard preset (AM270, AM650,
FM238 or FM476) and frequency, then put into asynchronous capture. The
firmware's `SubGhzWorker` turns the captured timings into (level, duration)
pairs on a worker thread. RadioGeddon feeds every pair to two consumers:

1. A `SubGhzReceiver` holding all of the firmware's *decodable* protocol
   decoders. When one matches, the parcel is serialized to a complete,
   loadable `.sub` text in RAM and stored in the session history.
2. When RAW recording is on, an in-RAM buffer of up to **16,384** signed
   durations (positive = carrier on, negative = off), written to a standard
   RAW `.sub` file when you save.

The start/stop order mirrors the firmware's own Sub-GHz subsystem
(`start_async_rx` → start worker; stop worker → `stop_async_rx` → idle →
sleep), and frequencies are validated before tuning because the HAL asserts on
out-of-band values.

**Scanner.** A light session keeps the radio powered; every UI tick the
scanner retunes to the next frequency, waits briefly for the AGC, and reads
RSSI.

**Frequency Hopper.** Uses the live receive session and retunes it without
powering the radio down (stop worker and capture → set frequency → restart),
cycling 315 / 390 / 433.92 / 868.35 MHz. When RSSI rises above −90 dBm it holds
the current frequency for about two seconds so a decode can finish.

**Transmit.** Replay opens the saved `.sub`, refuses files it should not send
(see [Features → Replay](FEATURES.md#authorized-signal-replay)), loads the
file's own modulation, and asks the firmware for permission to transmit
(`subghz_devices_set_tx`, which applies the device's regional rules) before
starting. RAW files are streamed by the firmware's file encoder worker;
decoded static protocols are re-encoded by the matching firmware encoder.

### Threads

| Thread | Runs | Rule |
|--------|------|------|
| GUI / event loop | scenes, views, ticks, file I/O, analysis | the only thread that touches views |
| Sub-GHz worker | pair callback → decoders, RAW buffer | records into mutex-protected history, then posts a custom event |
| File encoder worker | RAW replay streaming | signals completion with a custom event |

## Storage

Recordings are standard Flipper `.sub` files in
`/ext/apps_data/radiogeddon/signals` (created on first launch), so they stay
interoperable with the stock Sub-GHz app and other tools:

- decoded signals use `Filetype: Flipper SubGhz Key File`;
- RAW captures use `Filetype: Flipper SubGhz RAW File` with `RAW_Data` lines
  of up to 512 values.

The database screen uses the firmware's file browser filtered to `.sub`.

## Analysis pipeline

`radiogeddon_analysis.c` builds every text report. It reads the parsed file
from `radiogeddon_storage` and, for RAW captures, loads up to the first
**4,096** timing samples into the engine:

- `radiogeddon_dsp` — pulse-width clustering used by the basic Signal Analyzer;
- `rg_analyzer` — the deeper engine behind Unknown Protocol Analysis and RAW
  similarity scoring.

Both engines are plain C with no firmware headers, so `test/` compiles and runs
them on a normal computer under AddressSanitizer and UndefinedBehaviorSanitizer.
See [Protocol Analysis](PROTOCOL_ANALYSIS.md) for the algorithms and how
results are labelled.

## Build and release

- `scripts/firmware_pins.sh` pins the exact SDK for each firmware family
  (Official 1.4.3, Unleashed unlshd-093, RogueMaster commit `38d7ae9`).
- `scripts/build_target.sh` downloads and checksums the SDK, asserts its API
  version, builds the `.fap`, and verifies its manifest with
  `scripts/verify_fap.py`.
- `.github/workflows/build.yml` runs that for all three firmware families plus
  host tests, link checks and lint, for every pull request (`ci.yml`) and every
  release tag (`release.yml`).
- Releases are created only from tags, after all builds pass; assets are
  staged in a draft, re-downloaded and checksum-verified before publishing, and
  each `.fap` gets a signed build-provenance attestation.

## Design principles

- **Standalone.** No companion app, network, or desktop processing is required.
- **Real data only.** Analysis runs on actual captures; there is no simulated
  reception or placeholder output.
- **Honest labelling.** Firmware decoder matches are `[CONFIRMED]`; statistics
  are `[HEURISTIC]`; engine inferences are `[HYPOTHESIS]`.
- **Firmware safety rails stay on.** Regional transmit rules are enforced by
  the firmware, dynamic/rolling-code protocols are never replayed, and builds
  are never re-labelled to load on a firmware they weren't compiled for.
- **Interoperable files.** Everything is stored as standard `.sub`, which keeps
  the door open for a future desktop companion (not part of this project).
