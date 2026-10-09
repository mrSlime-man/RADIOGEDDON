# RADIOGEDDON

RadioGeddon — an open-source, **standalone** Sub-GHz radio analysis toolkit for
the Flipper Zero. Scan, capture, decode, analyze, compare and replay radio
signals, with protocol identification and rolling-code detection — all on the
device, driven entirely by the Flipper's buttons.

> **Status:** builds and passes the SDK API-compatibility check on official
> firmware (API 87.1) and Unleashed (API 88.9). **On-device behaviour is not
> yet verified on physical hardware** — see
> [`docs/HARDWARE_TEST_PLAN.md`](docs/HARDWARE_TEST_PLAN.md).

## Features

- **Receiver** — live RSSI, automatic protocol decoding/identification, and
  rolling-code flagging.
- **RAW Record** — capture raw signals to standard `.sub` files.
- **Saved Signals** — browse, inspect and delete recordings in `/ext/subghz`.
- **Compare Signals** — similarity score between two RAW captures.
- **Frequency Hopper** — scan common bands and stop on activity.
- **Replay** — re-transmit authorized RAW recordings, with an on-air warning
  and a firmware region check.
- **Settings** — choose frequency and modulation.

Recordings are ordinary Flipper `.sub` files in `/ext/subghz`, so they
interoperate with the stock Sub-GHz app.

## Install

Download the `.fap` that matches your firmware from
[`dist/release/`](dist/release/) and copy it to `apps/Sub-GHz/` on the SD card
(or drag it in with qFlipper). Full steps: [`docs/INSTALL.md`](docs/INSTALL.md).

| Firmware | File |
|----------|------|
| Official | `dist/release/official/radiogeddon-official.fap` |
| Unleashed | `dist/release/unleashed/radiogeddon-unleashed.fap` |
| RogueMaster | build from source against the RM SDK (see compatibility doc) |

Installing the wrong build triggers an API-version mismatch — pick the one for
your firmware. Details: [`docs/FIRMWARE_COMPATIBILITY.md`](docs/FIRMWARE_COMPATIBILITY.md).

## Build from source

```bash
pip install ufbt

# For the firmware you run (example: official stable)
ufbt update --channel release
ufbt                 # -> dist/radiogeddon.fap

# Build packaged artifacts for official + Unleashed at once:
./scripts/build_release.sh
```

See [`docs/FIRMWARE_COMPATIBILITY.md`](docs/FIRMWARE_COMPATIBILITY.md) for
building against Unleashed/RogueMaster SDKs.

## Documentation

- [Installation](docs/INSTALL.md)
- [Firmware compatibility](docs/FIRMWARE_COMPATIBILITY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Hardware test plan](docs/HARDWARE_TEST_PLAN.md)
- [Changelog](CHANGELOG.md)

## Legal

Receiving is passive. **Transmitting (replay) is regulated** — only transmit on
frequencies and devices you are legally authorized to operate. Rolling-code
devices cannot be meaningfully replayed and are flagged as such. RadioGeddon
honours the firmware's region/transmit policy and never bypasses it.

## License

MIT — see [LICENSE](LICENSE).
