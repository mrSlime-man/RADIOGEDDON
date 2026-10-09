# Changelog

All notable changes to RadioGeddon are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.0] - 2026-10-09

First unified release, reconciling the two development branches
(`subghz-analyzer` and `flipper-release`) into one standalone Sub-GHz analysis
toolkit for the Flipper Zero.

### Modules
- **Scanner** — live per-frequency RSSI sweep; select a frequency to receive.
- **Receive & Record** — live protocol decoding/identification with de-duplicated
  history, RSSI meter, and RAW capture to a standard `.sub` file.
- **Frequency Hopper** — cycles common bands, holding on activity so a decode can
  complete.
- **Signal Analyzer** — pulse-timing clusters and base `Te` for RAW; bit/field
  breakdown for decoded protocols.
- **Unknown Protocol Analysis** — pure signal engine inferring line encoding
  (PWM/PPM/Manchester) with confidence, frames, repeats, extracted bits, a
  constant-vs-changing field map and a device-ID candidate.
- **Crypto Analysis** — static vs. dynamic (rolling) classification and key-byte
  variety, with no key recovery.
- **Comparator** — field-by-field diff plus a RAW timing-similarity score.
- **Database** — browse / open / delete recordings (with delete confirmation).
- **Replay** — authorized transmit of RAW and static-protocol captures,
  reproducing the exact modulation (standard or file custom preset), refusing
  rolling-code protocols, and honouring the firmware's region policy.

Every analysis result is labelled `[CONFIRMED]` (a firmware decoder matched),
`[HEURISTIC]`, or `[HYPOTHESIS]`; identification is never confused with a guess,
and no cryptographic key is recovered or predicted.

### Firmware support
- One source tree builds against **Official** (ufbt release, API 87.1),
  **Unleashed** (`unlshd-093`, API 88.9) and **RogueMaster** (built against the
  RogueMaster firmware source with its own `fbt`, API 88.16). Each ships as its
  own `.fap`; embedded API metadata is never faked to force a mismatched load.
- Radio paths were diffed against OFW and RogueMaster firmware source; the
  preset enum and `begin()`/region differences are handled.

### Fixed (relative to the pre-merge branches)
- **Critical:** device presence no longer gated on `subghz_devices_begin()`'s
  return, which is `false` for the internal cc1101 and would have reported the
  built-in radio as absent on every real device.
- Replay reproduces the capture's exact modulation (incl. custom-preset data)
  and refuses unknown presets instead of transmitting on the wrong one.
- Deleting a recording now requires confirmation.

### Testing
- 55 host-side unit checks (DSP parsing + analysis engine) built `-Werror`
  under AddressSanitizer/UBSan; CI builds all three firmware targets.

### Not verified
- **On-device behaviour is unverified.** All physical-hardware tests in
  `docs/HARDWARE_CHECKLIST.md` remain UNVERIFIED pending a real device; see
  `docs/VERIFICATION.md` for the evidence-backed status.
