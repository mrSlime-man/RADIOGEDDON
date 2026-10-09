# Changelog

All notable changes to RadioGeddon are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project aims
to follow [Semantic Versioning](https://semver.org/) (pre-1.0.0 releases use
`-beta.N` suffixes).

## [Unreleased]

_Nothing yet._

## [1.0.0-beta.1] - 2026-10-09

First public beta: the two development lines are unified into one standalone
Sub-GHz analysis toolkit, published with multi-firmware builds, documentation
and a reproducible release pipeline. **On-device behaviour is not yet verified
on physical hardware** — see `docs/VERIFICATION.md`.

### Added
- **Modules:** Sub-GHz Scanner, Frequency Hopper, RAW Signal Capture, Protocol
  Identification (firmware decoders), Signal Analyzer, Unknown Protocol Analysis
  (PWM/PPM/Manchester hypotheses, framing, bit extraction), Signal Comparison
  with a RAW timing-similarity score, Device ID Candidate Detection, Rolling
  Code Classification, Cryptographic Structure Heuristics, Signal Database, and
  Authorized Signal Replay. See `docs/FEATURES.md`.
- `[CONFIRMED]` / `[HEURISTIC]` / `[HYPOTHESIS]` labelling across all analysis
  output; no key recovery, decryption or rolling-code prediction.
- **Three firmware builds** from one source tree — Official (API 87.1),
  Unleashed (88.9) and RogueMaster (88.16) — each against its own, SHA-256
  pinned SDK; API and `.fap` manifest verified for every artifact.
- **CI/release pipeline:** a shared build workflow (links, 55 host checks under
  ASan/UBSan, three firmware builds, lint) runs on every PR and release tag;
  tag releases rebuild from the tagged commit, verify uploaded assets against
  `SHA256SUMS`, attach build-provenance attestations, and publish.
- Branding (logo, README banner, social preview), full `docs/` set, community
  health files (Contributing, Code of Conduct, Security, issue/PR templates)
  and host-testable analysis engine (`helpers/rg_analyzer.*`).

### Fixed (relative to the pre-merge branches)
- **Critical:** device presence is no longer gated on `subghz_devices_begin()`'s
  return value (which is `false` for the internal CC1101), which would have
  reported the built-in radio as absent on every real device.
- Replay reproduces the capture's exact modulation (including a file's custom
  CC1101 register set) and refuses unknown presets instead of transmitting on
  the wrong one.
- Deleting a recording now requires confirmation.
- Retained analyzer-branch stabilization: firmware-matching RX start/stop order,
  the TX region gate, replay not mutating the stored file, cross-thread GUI
  safety, and invalid-frequency guards.

### Known limitations
- No on-device hardware verification yet (`docs/HARDWARE_CHECKLIST.md`).
- Settings are not persisted across launches; internal radio only; RAW capture
  capped at 16,384 samples (engine analysis uses the first 4,096).

[Unreleased]: https://github.com/mrSlime-man/RADIOGEDDON/compare/v1.0.0-beta.1...HEAD
[1.0.0-beta.1]: https://github.com/mrSlime-man/RADIOGEDDON/releases/tag/v1.0.0-beta.1
