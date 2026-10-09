# Changelog

All notable changes to RadioGeddon are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.0] - 2026-10-09

First release of the standalone Flipper Zero application.

### Added
- Standalone Sub-GHz `.fap` built on the portable `subghz_devices` radio layer.
- **Receiver**: live RSSI, protocol decoding and identification, rolling-code
  flagging, decode counter.
- **RAW Record**: capture RAW samples to a standard `.sub` file with a live
  sample counter; name-and-save flow.
- **Saved Signals**: browse `/ext/subghz`, view file details, delete.
- **Compare Signals**: tolerant RAW timing comparison with a 0–100 similarity
  score and verdict, gated on frequency/preset agreement.
- **Frequency Hopper**: cycles 315 / 433.92 / 868.35 / 915 MHz, lingers on
  active frequencies, skips frequencies the radio rejects.
- **Replay**: streams RAW recordings for authorized transmit, with an explicit
  on-air confirmation and a firmware region check (never bypassed).
- **Settings**: frequency and modulation (AM270 / AM650 / FM238 / FM476).
- Defensive radio wrapper with explicit state, safe teardown on every exit,
  and clear on-screen errors instead of crashes.
- Release tooling (`scripts/build_release.sh`) and CI building the official and
  Unleashed targets.
- Documentation: install, firmware compatibility, troubleshooting, and a
  hardware test plan.

### Firmware compatibility
- Verified to compile and pass the SDK API check against **official 1.4.3
  (API 87.1)** and **Unleashed unlshd-093 (API 88.9)** from one unmodified
  source tree. RogueMaster is source-compatible (build against its SDK).

### Not yet verified
- **On-device behaviour is unverified.** All physical-hardware tests in
  `docs/HARDWARE_TEST_PLAN.md` are marked `UNVERIFIED` pending a real device.
