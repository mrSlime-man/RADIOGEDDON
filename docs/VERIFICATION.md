# Verification Status

What has been verified and how, and what has not. Integrity rule: nothing is
marked hardware-verified without evidence from a physical device.

## Environment at time of writing

- No Flipper Zero connected (no USB device / `/dev/ttyACM*`). Hardware steps
  could not be run and are all marked UNVERIFIED.

## Verified in this environment (evidence-backed)

| Area | Method | Result |
|------|--------|--------|
| Official build | ufbt `release` SDK (fw 1.4.3, f7, API 87.1) | **Pass**, no warnings, APPCHK + lint clean; `radiogeddon-official.fap` |
| Unleashed build | ufbt index `up.unleashedflip.com` (`unlshd-093`, API 88.9) | **Pass**, APPCHK clean; `radiogeddon-unleashed.fap` |
| RogueMaster build | RogueMaster firmware source, `./fbt fap_radiogeddon` (API 88.16) | **Pass**, APPCHK clean; `radiogeddon-roguemaster.fap` |
| DSP/parse unit tests | `make -C test check` (`test_dsp`) | **Pass** — 31 checks |
| Analysis-engine tests | `make -C test check` (`test_analyzer`) | **Pass** — 24 checks |
| Memory safety of tests | built `-Werror` under `-fsanitize=address,undefined` | **Pass**, no ASan/UBSan reports |
| RX/TX sequencing | cross-checked against OFW `subghz_txrx.c` and RM radio source | Matches firmware |

Total host coverage: **55 checks, 0 failures**.

## Defects found and fixed (from firmware-source review of the merge)

1. **Internal radio reported absent on hardware (critical).**
   `is_device_present()` returned `subghz_devices_begin() && is_connect()`, but
   the internal cc1101's interconnect `begin` is `NULL`, so `subghz_devices_begin()`
   returns `false` in both Official and RogueMaster. Every radio screen would
   have shown "No radio" on a real device. Fixed to use `is_connect()` after an
   ignored-return `begin()`. (Only firmware-source review caught this; it
   compiles and passes host tests either way.)
2. **Replay modulation fidelity.** Replay now reproduces the capture's exact
   modulation, including loading a `FuriHalSubGhzPresetCustom` register array
   from `Custom_preset_data`; an unrecognised preset is refused rather than
   transmitted on a wrong/default modulation.
3. **Destructive delete.** Deleting a saved recording now requires an explicit
   confirmation.
   (Earlier stabilization fixes from the analyzer branch are retained: RX
   ordering, TX region gate, replay no longer mutating the stored file,
   cross-thread GUI safety, invalid-frequency guards.)

## Signal analysis engine (new)

`helpers/rg_analyzer.c` is pure, firmware-independent and host-tested:
clustering + base-Te estimate, encoding hypothesis (PWM/PPM/Manchester) with
confidence, frame segmentation, repeated-frame detection, PWM bit extraction,
constant-vs-changing field diff, device-ID candidate, and RAW similarity.
Every result is labelled `[HYPOTHESIS]`; no key recovery or decryption is
performed or claimed.

## NOT verified (requires physical hardware)

All radio behaviour and the end-to-end workflow. See
[HARDWARE_CHECKLIST.md](HARDWARE_CHECKLIST.md). In particular:

- Real over-the-air reception and live protocol decoding.
- RAW capture fidelity and replay producing a working transmission.
- The analysis engine's inferences against real captured signals (host tests
  use synthetic waveforms).
- Regional TX enforcement actually blocking disallowed frequencies.
- Long-run memory stability and absence of radio-threading crashes.
- That the three per-firmware `.fap`s load and run on their matching firmware.

## How to update this file

After running `HARDWARE_CHECKLIST.md` on a device, move each confirmed item
into the "Verified" table with the date, firmware name + version, and a
one-line evidence note (log excerpt or observed behaviour).
