# Firmware Compatibility

RadioGeddon is a standalone external app (`.fap`) for the Flipper Zero. It uses
only public SDK APIs — chiefly the portable `subghz_devices` radio layer and the
firmware's `subghz` protocol library — so it needs no firmware patches. It comes
in two editions (see [Features → Editions](FEATURES.md#editions)), and because
each firmware family publishes its own SDK, **each family gets its own `.fap`,
compiled against that family's SDK**.

## Supported firmware (v1.0.0-beta.7)

| Firmware | Edition | Built against | SDK API | Download |
|----------|---------|---------------|---------|----------|
| **Official** | Catalog | Flipper Zero firmware **1.4.3** SDK | **87.1** | `radiogeddon-catalog-official.fap` |
| **RogueMaster** | Full | RogueMaster source at commit [`38d7ae9`](https://github.com/RogueMaster/flipperzero-firmware-wPlugins/commit/38d7ae9ae7eb2d25b31cea9b9fcf88fd11c1f3d3), built with its own `fbt` | **88.16** | `radiogeddon-full-roguemaster.fap` |
| **Momentum** | Full | Momentum **mntm-012** SDK (tag commit [`e1784e7`](https://github.com/Next-Flip/Momentum-Firmware/commit/e1784e7418d8b074e971983ceb6fef0f37e52ae4)) | **87.1** | `radiogeddon-full-momentum.fap` |
| **Unleashed** | Full | Unleashed **unlshd-093** SDK | **88.9** | `radiogeddon-full-unleashed.fap` |

All four are built from the same source tree by CI. "Supported" here means the
build compiles cleanly against that firmware's own SDK, passes the SDK's
API-compatibility check (`APPCHK`), carries a verified manifest, and every
symbol it imports is exported by that SDK's `api_symbols.csv`
(`scripts/verify_fap.py --symbols`). On-device behaviour is not yet
independently verified on any firmware; a tester has reported that the beta 2
RogueMaster build launches and works (see [VERIFICATION.md](VERIFICATION.md)).

The exact SDK URLs, SHA-256 checksums and the RogueMaster commit are pinned in
[`scripts/firmware_pins.sh`](../scripts/firmware_pins.sh); every release lists
them again in its `BUILD_INFO.txt`.

## Momentum

Momentum is a first-class target with its own build, not a renamed copy of
another one:

- **SDK**: `flipper-z-f7-sdk-mntm-012.zip` from the
  [mntm-012 GitHub release](https://github.com/Next-Flip/Momentum-Firmware/releases/tag/mntm-012),
  SHA-256 `c90530c3…08cc2`, the same file and checksum Momentum's own update
  index (`up.momentum-fw.dev/firmware/directory.json`, release channel) lists.
- **API**: Momentum numbers its API like Official (**87.1**), but its symbol
  table differs from Official's in 522 entries, and its Sub-GHz headers differ
  too. Among them: the device vtable (`SubGhzDeviceInterconnect`) has an extra
  `check_tx` entry and its `begin` hook takes a `SubGhzDeviceConf`
  (extended range, region bypass, amp settings), the decoder base has
  `get_hash_data_long`, the receiver an ignore filter, the file encoder worker a
  progress call. Compiling against Momentum's own headers is what makes the
  structure layouts match the firmware the app runs on.
- **Radio ranges**: Momentum's `furi_hal_subghz_is_frequency_valid()` accepts
  281–361, 378–481 and 749–962 MHz (as RogueMaster and Unleashed do); Official
  accepts 300–348, 387–464 and 779–928 MHz. The Full edition measures the
  ranges the radio in use accepts at run time (Settings → Radio bands), so it
  never relies on a table.
- **Transmit**: Momentum's own transmit check (`subghz_devices_set_tx` →
  `check_tx`, which honours its extended-range and region settings) is left
  in force; RadioGeddon does not patch or bypass it.

## How the API-version gate works

When a `.fap` is built, the SDK's API version (`MAJOR.MINOR`) is written into
the file's manifest. When you open the app, the firmware:

1. refuses it outright unless the manifest's API **major** version equals its
   own (the manifest check compares major versions only), and then
2. resolves every firmware function the app imports. A firmware whose API
   **minor** version is older than the one the app was built with may lack some
   of those functions, and the load then fails with a missing-imports error.

So one `.fap` cannot serve every firmware, and a build keeps loading as a
firmware family ships updates within the same major API version:

| Artifact | Built with | Expected to load on |
|----------|------------|---------------------|
| `radiogeddon-catalog-official.fap` | 87.1 | Official firmware with API 87.x, x ≥ 1 |
| `radiogeddon-full-roguemaster.fap` | 88.16 | RogueMaster with API 88.x, x ≥ 16 |
| `radiogeddon-full-momentum.fap` | 87.1 | Momentum with API 87.x, x ≥ 1 |
| `radiogeddon-full-unleashed.fap` | 88.9 | Unleashed with API 88.x, x ≥ 9 |

Momentum and Official share API 87.1, so the firmware's version check alone
would not stop the wrong one from loading — **install the file named for your
firmware**. The structure layouts differ (see above), so a build for the other
firmware may misbehave even if it starts.

If a future firmware release changes its API **major** version, the app will be
refused ("API mismatch") until a new build is published. You can always build
against your own firmware's SDK — see below.

> RadioGeddon never edits the manifest to make a build load on a firmware it
> wasn't compiled for. Doing so bypasses the firmware's ABI safety check and can
> crash the device.

You can inspect any `.fap`'s manifest and imports yourself:

```bash
python3 scripts/verify_fap.py radiogeddon-full-momentum.fap --api 87.1 --json \
    --symbols <Momentum SDK>/sdk_headers/f7_sdk/targets/f7/api_symbols.csv
```

## Which file should I install?

Check your firmware in **Settings → About** on the Flipper (or in qFlipper):

- Version like `1.4.3` from flipperzero.one → **Official** → Catalog edition
- Version starting with `mntm-` → **Momentum** → Full edition
- Version starting with `unlshd-` → **Unleashed** → Full edition
- RogueMaster build (version string mentions RM / RogueMaster) → **RogueMaster** → Full edition

Other forks (Xtreme and others) are not built or tested. Build from source
against that fork's SDK.

## Newer firmware

The **Firmware watch** workflow checks every week for firmware newer than the
pinned SDKs (Official release and release candidate, Unleashed, Momentum,
RogueMaster), with its API version, and canary-builds the app against every
newer Official (Catalog edition), Unleashed or Momentum (Full edition) SDK.
State on 2026-10-10:

- **Official 1.5.1-rc** (release candidate) moves to API **88.2**, so
  `radiogeddon-catalog-official.fap` (API 87.1) will be refused on it. No
  release targets it yet. The Catalog edition builds against the 1.5.1-rc SDK
  (compile, `APPCHK`, manifest and imported-symbol checks pass), but nothing
  has been tested on a device with it. If you run the release candidate, build
  from source as below with `ufbt update --hw-target f7 --channel rc`. The
  Official pin moves to 1.5.1 once it is released and the hardware checklist
  has been run on it.
- Unleashed unlshd-093, Momentum mntm-012 and RogueMaster `38d7ae9` are the
  newest of their families.

## Building for your exact firmware

```bash
source scripts/firmware_pins.sh && pip install "ufbt==${UFBT_VERSION}"
scripts/build_target.sh catalog-official   # or: full-roguemaster | full-momentum | full-unleashed
```

To target a different firmware version, write the edition's source, deploy
its SDK with uFBT and build:

```bash
python3 scripts/stage_edition.py full ../radiogeddon-full   # or: catalog
cd ../radiogeddon-full
UFBT_HOME=$PWD/.ufbt-custom ufbt update --hw-target f7 --url <SDK zip URL>
UFBT_HOME=$PWD/.ufbt-custom ufbt                            # -> dist/radiogeddon_full.fap
python3 scripts/verify_fap.py dist/radiogeddon_full.fap --api <that SDK's API> \
    --symbols .ufbt-custom/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv
```

The SDK's API version is the `Version` row in
`$UFBT_HOME/current/sdk_headers/f7_sdk/targets/f7/api_symbols.csv`. For
RogueMaster, `scripts/build_target.sh full-roguemaster` stages the Full edition
into `applications_user/radiogeddon` of a RogueMaster checkout and runs
`./fbt fap_radiogeddon_full`; set `ROGUEMASTER_REF` in `firmware_pins.sh` to
build against a different commit.

## Radio-layer differences that were checked

The radio paths RadioGeddon relies on were compared against the firmware
sources:

- **`subghz_devices_begin()`**: for the internal CC1101 the device's `begin`
  hook is empty, so the call reports `false` on every firmware family.
  RadioGeddon therefore decides whether a radio is present from
  `subghz_devices_is_connect()`, not from `begin()`'s return value. (Gating on
  `begin()` would make the built-in radio look absent on real hardware.)
- **Preset enum**: RogueMaster adds an extra preset to `FuriHalSubGhzPreset`,
  which shifts the numeric values. RadioGeddon only uses the named preset
  constants, so each build gets the right values for its own SDK.
- **Tuning ranges**: `furi_hal_subghz_is_frequency_valid()` is 300–348 /
  387–464 / 779–928 MHz on Official 1.4.3 and 281–361 / 378–481 / 749–962 MHz
  on RogueMaster `38d7ae9`, Momentum mntm-012 and Unleashed unlshd-093 (read
  from each firmware's `targets/f7/furi_hal/furi_hal_subghz.c`). The Full
  edition's band probe (`rg_range_probe_bands`) is tested against both tables.
- **Region / transmit policy**: the firmware's own checks are left in force on
  every family; RadioGeddon does not bypass them. The Catalog edition adds its
  own check in front of them (next section).

## Transmitting in each edition

Every transmission goes through the firmware's radio driver
(`subghz_devices_set_tx`), which applies the firmware's transmit rules. Before
that:

- **Catalog edition** (`RG_FEATURE_REGION_TX_GATE`): the app requires that the
  radio in use accepts the frequency, that `furi_hal_subghz_is_frequency_valid()`
  accepts it, that the firmware has a provisioned region
  (`furi_hal_region_is_provisioned()`) and that the region allows the frequency
  (`furi_hal_region_is_frequency_allowed()`). If any of these fails — including
  when the region is unknown — it refuses and shows why ("Region EU does not
  allow TX on 315.00 MHz"). There is no region selector in the app.
- **Full edition**: the app only requires that the radio in use accepts the
  frequency; the installed firmware's own rules decide the rest. This edition
  adds no app-specific regional restriction.

Rolling-code protocols are refused for replay in both editions.

## Hardware

Builds target Flipper Zero hardware revision `f7` and its internal CC1101 radio
(`cc1101_int`). An external CC1101 module can be chosen in Settings; it is
driven by the firmware's own `cc1101_ext` driver, which the firmware installs
as a plugin in `/ext/apps_data/subghz/plugins`. If that plugin is missing, or
no module answers on the SPI bus, the internal radio is used. In the Catalog
edition, transmitting through it is checked against the firmware's region the
same way as the internal radio.
