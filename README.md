# RadioGeddon

**A standalone Sub-GHz radio analysis toolkit for Flipper Zero.**

RadioGeddon scans, captures, decodes, analyzes, compares and (where authorized
and legal) replays Sub-GHz radio signals entirely on the device — no computer,
phone, server or internet connection required.

It is built on the official Flipper Zero Sub-GHz subsystem and GUI stack, so it
reuses the firmware's real radio drivers and protocol decoders rather than
reimplementing them. Recordings are stored as standard `.sub` files, compatible
with the firmware's own Sub-GHz app.

---

## Status

This is an actively developed toolkit. It **builds cleanly** against the
Flipper Zero release SDK (firmware 1.4.3, API 87.1) with `ufbt`, and the
firmware-independent signal-processing logic is covered by host unit tests.

> **Hardware testing:** The code targets real hardware via the official
> `subghz_devices_*` / `furi_hal_subghz` APIs and performs no simulated
> reception or fabricated analysis. However, it has **not** yet been verified on
> a physical Flipper Zero in this development environment. On-device validation
> of the milestone flow (receive → save → display → reopen) is the next step and
> is tracked in [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md).

See [`docs/CAPABILITIES.md`](docs/CAPABILITIES.md) for the precise list of
implemented vs. unimplemented features, and
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) for hardware/API limitations.

---

## Features

| Module | What it does |
| --- | --- |
| **Scanner** | Sweeps common Sub-GHz frequencies and shows live per-frequency RSSI bars read from the radio. Select a frequency to jump straight into receive. |
| **Frequency Hopper** | Continuously cycles a short list of common bands, pausing to decode whenever activity (RSSI above the noise floor) is detected. |
| **Receive & Record** | Live-decodes known protocols using the firmware's decoders, shows an RSSI meter and a decoded-signal list, and can capture the raw timing stream to a `.sub` file. |
| **Signal Analyzer** | Inspects pulse timing (clustered symbol widths, estimated base `Te`, edge counts) for RAW captures, or bit/field structure for decoded protocols. |
| **Crypto Analyzer** | Classifies signals as fixed (static) vs. rolling-code/dynamic (encrypted hop code) and reports key-byte variety — **without** claiming key recovery. |
| **Comparator** | Compares two recordings field-by-field and highlights constant vs. changing fields (e.g. a rolling counter increment). |
| **Database** | Browses, opens and deletes recordings stored on the SD card. |
| **Replay** | Transmits compatible, non-protected signals for authorized testing. Rolling-code/dynamic protocols are intentionally not transmitted; regional transmit limits are enforced by the firmware. |

Every analysis result is explicitly labelled **`[CONFIRMED]`** (a firmware
decoder matched the signal) or **`[HEURISTIC]`** (a guess derived from signal
statistics), so confirmed identification is never confused with estimation.

---

## Building

Prerequisites: [`ufbt`](https://pypi.org/project/ufbt/) (the micro Flipper Build
Tool).

```bash
pip install ufbt          # once
ufbt                      # build -> dist/radiogeddon.fap
```

The compiled application artifact is produced at `dist/radiogeddon.fap`.

### Running the unit tests

The firmware-independent DSP/parsing logic has host-side tests:

```bash
make -C test check
```

---

## Installing on a Flipper Zero

1. Build `dist/radiogeddon.fap` as above (or copy the prebuilt artifact).
2. Copy it to the SD card under `apps/Sub-GHz/` using qFlipper, the mobile app,
   or `ufbt launch` while the device is connected over USB:
   ```bash
   ufbt launch
   ```
3. On the device: **Apps → Sub-GHz → RadioGeddon**.

Recordings and analysis are stored on the SD card under
`/ext/apps_data/radiogeddon/signals`.

### Firmware compatibility

Targets the **official** Flipper Zero firmware. It uses only public,
documented Sub-GHz APIs and should also build for RogueMaster and other
forks that keep those APIs; where a fork diverges, see
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md).

---

## Controls

- **Scanner:** Up/Down select a frequency, OK jumps into receive, Back exits.
- **Frequency Hopper:** cycles bands automatically; Up/Down scroll decoded
  signals, OK saves the highlighted decode, Back exits.
- **Receive & Record:** Up/Down scroll decoded signals, OK saves the highlighted
  decode, **Left toggles RAW recording**, Back exits.
- **Database / menus:** Up/Down navigate, OK selects, Back returns.

---

## Legal & responsible use

RadioGeddon is intended for education, research, and testing of devices you own
or are explicitly authorized to test. It does not break encryption, recover
keys, or defeat rolling codes. Transmission respects the firmware's regional
restrictions. You are responsible for complying with the radio regulations in
your jurisdiction.

---

## Architecture

```
radiogeddon.c / .h         App context, ViewDispatcher + SceneManager wiring
scenes/                    One file per screen (start, scanner, receiver, ...)
views/                     Custom canvas views (scanner sweep, live receiver)
helpers/
  radiogeddon_subghz.*     Radio wrapper: device, decode, RAW capture, TX
  radiogeddon_storage.*    SD-card layout and .sub parsing
  radiogeddon_history.*    Volatile per-session decoded-signal list
  radiogeddon_analysis.*   Describe / analyze / crypto / compare
  radiogeddon_dsp.*        Pure, testable timing-parse & clustering logic
test/                      Host unit tests for the DSP helpers
```

The decoder/analysis layer is modular: adding support for another protocol is a
matter of enabling it in the firmware protocol registry — no changes to the
analysis UI are required.

A future desktop companion could import these `.sub` recordings for heavier
analysis; the on-device file format is kept standard to allow it. That
companion is **not** part of this project.

---

## License

MIT — see [`LICENSE`](LICENSE).
