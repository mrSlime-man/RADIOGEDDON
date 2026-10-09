# Roadmap

Where RadioGeddon is headed. This is a plan, not a promise — priorities shift
with what hardware testing finds and what contributors pick up. Dates are
deliberately omitted.

## Now — `1.0.0-beta.2`

`1.0.0-beta.2` fixes an out-of-memory crash on launch that made `1.0.0-beta.1`
unusable on RogueMaster. The full toolkit is implemented and builds for Official, Unleashed and
RogueMaster, with 55 host-test checks and a verified release pipeline. See
[Features](FEATURES.md) for the complete list.

## Next: hardware verification (the gating milestone)

Nothing advances to a stable `1.0.0` until the app is exercised on real
hardware. The goal is to work through [the checklist](HARDWARE_CHECKLIST.md) on
all three firmware families and record evidence in
[VERIFICATION.md](VERIFICATION.md).

- [ ] End-to-end workflow (W1–W10) confirmed on at least one firmware.
- [ ] Each per-firmware build confirmed to load on its firmware (L1–L3).
- [ ] Receive/decode, RAW capture fidelity, and authorized replay confirmed
      against real remotes.
- [ ] Regional transmit enforcement confirmed to block disallowed frequencies.
- [ ] A memory-stability pass over repeated capture/analyse/replay cycles.

This is where help is most valuable — see [Contributing](../CONTRIBUTING.md).

## Toward a stable 1.0.0

- [ ] Fix whatever hardware testing surfaces.
- [ ] Persist Settings (frequency, modulation) across launches.
- [ ] Save analysis reports to the SD card as text.
- [ ] Rename recordings from the Database screen.

## Later

- [ ] External CC1101 module support surfaced in the UI (the radio layer
      already uses the portable device API).
- [ ] Custom frequency entry and a custom modulation/preset editor.
- [ ] Richer analysis: more encodings, CRC/checksum guesses, bit-field views.
- [ ] A larger library of reference captures for regression testing.
- [ ] Localisation of on-screen text.

## Under consideration (not committed)

- A desktop companion that imports these `.sub` recordings for heavier,
  off-device analysis. The on-device file format is kept standard so this stays
  possible; it is not being built as part of this project yet.

## Explicitly out of scope

RadioGeddon will not add key recovery, rolling-code prediction or bypass,
brute-forcing, jamming, or any removal of the transmit safeguards. These are
non-goals by design — see the [Security Policy](../SECURITY.md).

## Suggesting a direction

Open a [feature request](https://github.com/mrSlime-man/RADIOGEDDON/issues/new?template=feature_request.yml)
or start a discussion. Contributions that move roadmap items forward are
welcome.
