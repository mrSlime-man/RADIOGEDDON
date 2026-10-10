# Verification Status

What has been verified, how, and what has not. Integrity rule: nothing is
marked hardware-verified without evidence from a physical device. As of
`1.0.0-beta.2`, the only physical-hardware results are tester reports (see
"Reported by users" below); no checklist item is independently verified, and
the "Not verified" section stands open.

## Environment

The project is developed and built in CI containers with no Flipper Zero
attached (no USB device, no `/dev/ttyACM*`). Every radio behaviour is therefore
unverified and is listed under "Not verified".

## Verified by automation (evidence-backed)

All of the following run in GitHub Actions on every pull request and every
release tag (`.github/workflows/ci.yml` → `build.yml`), and can be reproduced
locally.

| Area | Method | Result |
|------|--------|--------|
| Official build | SDK 1.4.3 pinned by SHA-256, API asserted 87.1, `.fap` manifest verified | Pass — `radiogeddon-official.fap` |
| Unleashed build | SDK unlshd-093 pinned by SHA-256, API asserted 88.9, manifest verified | Pass — `radiogeddon-unleashed.fap` |
| RogueMaster build | RogueMaster source at commit `38d7ae9`, built with its own `fbt`, API asserted 88.16, manifest verified | Pass — `radiogeddon-roguemaster.fap` |
| Lint | `ufbt lint` (clang-format) | Pass, no warnings |
| DSP/parse unit tests | `make -C test check` → `test_dsp` | Pass — 31 checks |
| Analysis-engine unit tests | `make -C test check` → `test_analyzer` | Pass — 89 checks |
| Scanner-logic unit tests | `make -C test check` → `test_scan` | Pass — 31 checks |
| Hopper-logic unit tests | `make -C test check` → `test_hop` | Pass — 42 checks |
| RAW-reader unit tests | `make -C test check` → `test_raw` | Pass — 29 checks |
| Memory safety of tested code | tests built `-Werror` under `-fsanitize=address,undefined` | Pass — no ASan/UBSan reports |
| Documentation links | `scripts/check_links.py` (offline link + anchor check) | Pass |
| `.fap` metadata | `scripts/verify_fap.py` parses `.fapmeta` and asserts magic, API, target, name, version, icon | Pass for all three artifacts |

Host-test total: **222 checks, 0 failures.** What the suite covers (synthetic
signals, not real captures):

- `test_dsp` — RAW `RAW_Data` parsing (incl. whitespace, signs, out-of-range),
  duration clustering, cluster sorting, and a RAW capture→file→reparse
  round-trip.
- `test_analyzer` — PWM, PPM and Manchester identification with exact bit
  recovery (both Manchester phases), Te estimation, timing peaks, noise share
  and quality grade, robustness to receiver-like noise and ±12 % jitter, frame
  grouping (identical repeats, two buttons, largest pattern first), alignment
  of a cut-off first frame, bit-length estimation, constant-vs-changing field
  maps and the ID candidate, identical results when fed in chunks with a split
  pulse, the decode and align APIs, streamed and in-memory RAW similarity, and
  degenerate input (empty, one pulse, one level only, pure noise).
- `test_scan` — scanner logic on synthetic RSSI sequences: no false triggers
  on noise, one count per burst, warm-up suppression, hysteresis, absolute
  minimum, floor tracking up and down, peak/count reset, median noise floor,
  band masks and CSV row formatting (including truncation).
- `test_hop` — hopper state machine on simulated time and RSSI: even time
  sharing on quiet bands, holding on a burst with no retunes until the hold
  expires, event history (channel, peak, duration), lock/unlock, decodes
  starting and extending holds, ring-buffer bounds, single/empty lists, and
  200 repeated hold/lock/retune cycles with no stuck state.
- `test_raw` — the streaming `RAW_Data` reader: values, signs, zeros, CRLF and
  a missing final newline, reads split at 5- and 13-byte and one-sample
  boundaries, corrupt tokens and lone minus signs, non-RAW files, a 6,000-sample file with one
  3,000-value line (bounded checkpoint table, seeking to any time resumes with
  the right samples, rewind), and `test/fixtures/raw_ref.sub` read end to end
  through the analyzer.

## Release-pipeline integrity

- Releases are produced only from a version tag by `release.yml`, after the full
  build pipeline passes. The workflow stages assets in a draft, re-downloads
  them, checks them against `SHA256SUMS`, and only then publishes; any failure
  deletes the draft.
- Each `.fap` carries a signed build-provenance attestation
  (`actions/attest-build-provenance`) linking it to the workflow run and the
  tagged commit. Toolchain builds are not byte-for-byte reproducible across
  machines, so provenance — not cross-machine hash equality — is the integrity
  guarantee.

## Code review of the pre-merge branches

The two development branches were reviewed against the firmware sources during
the merge; defects found and fixed:

1. **Critical — internal radio reported absent.** Device presence had been
   gated on `subghz_devices_begin()`'s return value, which is `false` for the
   internal CC1101 (its interconnect `begin` hook is empty). On real hardware
   every radio screen would have shown "No radio". Fixed to use
   `subghz_devices_is_connect()` after an ignored-return `begin()`. This
   compiles and passes host tests either way, so only source review caught it —
   and it is itself a prime item for hardware confirmation (checklist W2).
2. **Replay modulation fidelity.** Replay reproduces the capture's exact
   modulation, including loading a custom CC1101 register array from a file's
   `Custom_preset_data`; an unrecognised preset is refused rather than
   transmitted on a default modulation.
3. **Destructive delete.** Deleting a recording now requires explicit
   confirmation.

Earlier stabilization fixes from the analyzer branch are retained: RX start/stop
ordering matching the firmware, the TX region gate via `subghz_devices_set_tx`,
replay no longer mutating the stored file, cross-thread GUI safety (worker
records to a mutex-protected history and posts an event), and invalid-frequency
guards before tuning.

## Reported by users on physical hardware

These are results reported by a tester, not independently reproduced or
logged by the project. They are recorded separately from the automated
evidence above and from the checklist's "Verified" bar.

| Date | Firmware | Build | Reported result |
|------|----------|-------|-----------------|
| 2026-10-09 | RogueMaster (version not recorded) | `v1.0.0-beta.1` | **Crash on launch**: "Flipper crashed and was rebooted — Out of memory", before the main menu. |
| 2026-10-09 | RogueMaster (version not recorded) | PR #3 (memory fix, later `v1.0.0-beta.2`) | The app launches to the main menu and works ("it is working"). Individual checklist items were not reported separately. |

The crash was caused by allocating about 110 KB of radio state (a 64 KB RAW
buffer, the protocol decoders and keystore, and the Sub-GHz worker) at app
start; see [Architecture](ARCHITECTURE.md). Checklist item W1 (launch) on
RogueMaster is therefore user-reported as passing; it still needs a logged
report through the hardware-report form to count as verified.

## NOT verified (requires physical hardware)

Everything about on-device radio behaviour, and the end-to-end workflow. See the
[hardware checklist](HARDWARE_CHECKLIST.md). In particular:

- Real over-the-air reception and live protocol decoding.
- RAW capture fidelity, and replay producing a working transmission.
- The internal-radio presence fix (defect 1) actually resolving "No radio" on a
  device.
- The analysis engine's inferences against real captured signals (host tests use
  synthetic waveforms only), including how its noise, jitter and peak
  thresholds behave on real receiver noise, and how long a whole-file analysis
  of a large capture takes on the SD card.
- Regional TX enforcement actually blocking disallowed frequencies on hardware.
- Long-run memory stability and absence of radio-threading crashes.
- That each per-firmware `.fap` loads and runs on its matching firmware.

## How this file is updated

After running the [hardware checklist](HARDWARE_CHECKLIST.md) on a device, move
each confirmed item into the "Verified" section with the date, firmware family
and version, and a one-line evidence note (log excerpt or observed behaviour).
Report results through the hardware-report issue form so they can be
corroborated.
