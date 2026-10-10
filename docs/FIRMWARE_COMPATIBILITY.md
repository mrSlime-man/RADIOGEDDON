# Firmware Compatibility

RadioGeddon is a standalone external app (`.fap`) for the Flipper Zero. It uses
only public SDK APIs — chiefly the portable `subghz_devices` radio layer and the
firmware's `subghz` protocol library — so it needs no firmware patches. Because
each firmware family publishes its own SDK API version, **each family gets its
own `.fap`**.

## Supported firmware (v1.0.0-beta.4)

| Firmware | Built against | SDK API | Download |
|----------|---------------|---------|----------|
| **Official** | Flipper Zero firmware **1.4.3** SDK | **87.1** | `radiogeddon-official.fap` |
| **Unleashed** | Unleashed **unlshd-093** SDK | **88.9** | `radiogeddon-unleashed.fap` |
| **RogueMaster** | RogueMaster source at commit [`38d7ae9`](https://github.com/RogueMaster/flipperzero-firmware-wPlugins/commit/38d7ae9ae7eb2d25b31cea9b9fcf88fd11c1f3d3), built with its own `fbt` | **88.16** | `radiogeddon-roguemaster.fap` |

All three are built from the same source tree by CI. "Supported" here means the
build compiles cleanly, passes the SDK's API-compatibility check (`APPCHK`), and
carries a verified manifest. On-device behaviour is not yet independently
verified on any firmware; a tester has reported that the RogueMaster build
launches and works (see [VERIFICATION.md](VERIFICATION.md)).

The exact SDK URLs, SHA-256 checksums and the RogueMaster commit are pinned in
[`scripts/firmware_pins.sh`](../scripts/firmware_pins.sh); every release lists
them again in its `BUILD_INFO.txt`.

## How the API-version gate works

When a `.fap` is built, the SDK's API version (`MAJOR.MINOR`) is written into
the file's manifest. When you open the app, the firmware:

1. refuses it outright unless the manifest's API **major** version equals its
   own (the manifest check compares major versions only), and then
2. resolves every firmware function the app imports. A firmware whose API
   **minor** version is older than the one the app was built with may lack some
   of those functions, and the load then fails with a missing-imports error.

So one `.fap` cannot serve every firmware (Official is on API 87, Unleashed and
RogueMaster on 88), and a build keeps loading as a firmware family ships
updates within the same major API version:

| Artifact | Built with | Expected to load on |
|----------|------------|---------------------|
| `radiogeddon-official.fap` | 87.1 | Official firmware with API 87.x, x ≥ 1 |
| `radiogeddon-unleashed.fap` | 88.9 | Unleashed with API 88.x, x ≥ 9 |
| `radiogeddon-roguemaster.fap` | 88.16 | RogueMaster with API 88.x, x ≥ 16 |

If a future firmware release changes its API **major** version, the app will be
refused ("API mismatch") until a new build is published. You can always build
against your own firmware's SDK — see below.

> RadioGeddon never edits the manifest to make a build load on a firmware it
> wasn't compiled for. Doing so bypasses the firmware's ABI safety check and can
> crash the device.

You can inspect any `.fap`'s manifest yourself:

```bash
python3 scripts/verify_fap.py radiogeddon-official.fap --api 87.1 --json
```

## Which file should I install?

Check your firmware in **Settings → About** on the Flipper (or in qFlipper):

- Version like `1.4.3` from flipperzero.one → **Official**
- Version starting with `unlshd-` → **Unleashed**
- RogueMaster build (version string mentions RM / RogueMaster) → **RogueMaster**

Other forks (Momentum, Xtreme and others) are not built or tested. They may load
one of these files if their API version satisfies the rule above; otherwise build
from source against that fork's SDK.

## Newer firmware

The **Firmware watch** workflow checks every week for firmware newer than the
pinned SDKs, with its API version, and builds the app against every newer
Official or Unleashed SDK. State on 2026-10-09:

- **Official 1.5.1-rc** (release candidate) moves to API **88.2**, so
  `radiogeddon-official.fap` (API 87.1) will be refused on it. No release
  targets it yet. The app builds against the 1.5.1-rc SDK (compile, `APPCHK`
  and manifest check pass), but nothing has been tested on a device with it.
  If you run the release candidate, build from source as below with
  `ufbt update --hw-target f7 --channel rc`. The Official pin moves to 1.5.1
  once it is released and the hardware checklist has been run on it.
- Unleashed unlshd-093 and RogueMaster `38d7ae9` are still the newest of their
  families.

## Building for your exact firmware

```bash
source scripts/firmware_pins.sh && pip install "ufbt==${UFBT_VERSION}"
scripts/build_target.sh official      # or: unleashed | roguemaster
```

To target a different firmware version, deploy its SDK with uFBT and build:

```bash
UFBT_HOME=$PWD/.ufbt-custom ufbt update --hw-target f7 --url <SDK zip URL>
UFBT_HOME=$PWD/.ufbt-custom ufbt                          # -> dist/radiogeddon.fap
python3 scripts/verify_fap.py dist/radiogeddon.fap --api <that SDK's API>
```

The SDK's API version is the `Version` row in
`$UFBT_HOME/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv`. For
RogueMaster, `scripts/build_target.sh roguemaster` stages the app into
`applications_user/radiogeddon` of a RogueMaster checkout and runs
`./fbt fap_radiogeddon`; set `ROGUEMASTER_REF` in `firmware_pins.sh` to build
against a different commit.

## Radio-layer differences that were checked

RogueMaster is not simply Unleashed, so the radio paths RadioGeddon relies on
were compared against the firmware sources:

- **`subghz_devices_begin()`**: for the internal CC1101 the device's `begin`
  hook is empty, so the call reports `false` on every firmware family.
  RadioGeddon therefore decides whether a radio is present from
  `subghz_devices_is_connect()`, not from `begin()`'s return value. (Gating on
  `begin()` would make the built-in radio look absent on real hardware.)
- **Preset enum**: RogueMaster adds an extra preset to `FuriHalSubGhzPreset`,
  which shifts the numeric values. RadioGeddon only uses the named preset
  constants, so each build gets the right values for its own SDK.
- **Region / transmit policy**: the firmware's region checks are left in force
  on every family; RadioGeddon does not bypass them.

## Hardware

Builds target Flipper Zero hardware revision `f7` and its internal CC1101 radio
(`cc1101_int`). An external CC1101 module can be chosen in Settings; it is
driven by the firmware's own `cc1101_ext` driver, which the firmware installs
as a plugin in `/ext/apps_data/subghz/plugins`. If that plugin is missing, or
no module answers on the SPI bus, the internal radio is used. The external
driver applies the firmware's region table when tuning, and RadioGeddon also
checks `furi_hal_region_is_frequency_allowed()` itself before transmitting
through it.
