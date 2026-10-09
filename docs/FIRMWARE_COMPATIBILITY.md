# Firmware Compatibility

RadioGeddon is a **standalone external application (`.fap`)** for the Flipper
Zero. It uses only public SDK APIs — chiefly the portable `subghz_devices`
radio layer and the `subghz` protocol library — which are present in official
firmware and in the Unleashed / RogueMaster family. **No firmware patches or
custom-firmware features are required.**

## The key fact: the API-version gate

Every Flipper firmware SDK publishes an **API version** (e.g. `87.1`). When a
`.fap` is built, that number is embedded in the file. At load time the firmware
refuses any `.fap` whose embedded API is incompatible with the running
firmware — an ABI mismatch would otherwise crash the device.

Official, Unleashed and RogueMaster track **different API versions**, so one
`.fap` will generally not load on all of them. The supported solution is to
ship **one `.fap` per firmware family, each built against that family's own
SDK**.

> **We never edit `application.fam` or the compiled metadata to fake an API
> version so a mismatched build will load.** If your firmware rejects a build,
> install the build that matches it (or build from source against your SDK).

## Verified builds

The identical source tree compiles **without modification** against all three
firmware families. RogueMaster is built against the RogueMaster firmware source
itself (not assumed from Unleashed), because it ships its own SDK/API and extra
radio presets.

| Firmware family | SDK / source used to verify | Embedded API | Status |
|-----------------|-----------------------------|--------------|--------|
| Official (stable) | ufbt `release` channel, fw 1.4.3 | `87.1` | ✅ Builds clean, APPCHK + lint pass |
| Unleashed | ufbt index `up.unleashedflip.com`, `unlshd-093` | `88.9` | ✅ Builds clean, APPCHK pass |
| RogueMaster | `RogueMaster/flipperzero-firmware-wPlugins` source, `./fbt fap_radiogeddon` | `88.16` | ✅ Builds clean, APPCHK pass |

> **Hardware note:** "Builds clean / APPCHK passes" means the compiler, linker
> and the SDK's API-compatibility check all succeed. On-device behaviour has
> **not** been verified on physical hardware — see
> [HARDWARE_CHECKLIST.md](HARDWARE_CHECKLIST.md).

## Which file do I install?

Install the artifact that matches your firmware (see
[INSTALL.md](INSTALL.md)):

| Firmware | File |
|----------|------|
| Official | `dist/release/official/radiogeddon-official.fap` |
| Unleashed | `dist/release/unleashed/radiogeddon-unleashed.fap` |
| RogueMaster | `dist/release/roguemaster/radiogeddon-roguemaster.fap` |

If the Flipper reports an *API version mismatch* or the app fails to open, you
installed the wrong build — use the one matching your firmware, or rebuild from
source.

## Building for your exact firmware

Official and Unleashed use [`ufbt`](https://pypi.org/project/ufbt/):

```bash
pip install ufbt

# Official (stable):
ufbt update --channel release && ufbt          # -> dist/radiogeddon.fap

# Unleashed:
UFBT_HOME=.ufbt-unleashed ufbt update --index-url https://up.unleashedflip.com/directory.json
UFBT_HOME=.ufbt-unleashed ufbt
```

`scripts/build_release.sh` automates the Official + Unleashed builds and writes
named artifacts and `SHA256SUMS` into `dist/release/`.

RogueMaster builds against the RogueMaster firmware source with its own `fbt`:

```bash
scripts/build_roguemaster.sh            # clones RM fw, stages the app, builds
# -> dist/release/roguemaster/radiogeddon-roguemaster.fap
```

The script copies this app into the RM tree's `applications_user/radiogeddon`
and runs `./fbt fap_radiogeddon`. You can also do it by hand inside a
RogueMaster checkout.

## Radio-layer differences we checked (RogueMaster vs Official)

RogueMaster is **not** merely Unleashed, so the radio paths this app relies on
were diffed against the RogueMaster firmware source:

- `subghz_devices_begin()` on RogueMaster passes a `SubGhzDeviceConf` (extended
  range / region-bypass flags) to the device; the portable wrapper hides this,
  and for the internal cc1101 `begin` is a no-op in every family. RadioGeddon
  does **not** gate device presence on `begin()`'s return (that would wrongly
  report the internal radio as absent — see the fix in `radiogeddon_subghz.c`).
- The preset enum gains `FuriHalSubGhzPreset2FSKDev12KAsync` in RogueMaster,
  shifting enum values. RadioGeddon only uses **named** preset constants, so the
  compiler resolves them correctly per SDK; no raw preset integers appear
  anywhere.
- Region/TX enforcement (`furi_hal_region` / `set_tx`) is honoured, not
  bypassed, on every family.

## Hardware target

Builds target the Flipper Zero `f7` hardware (STM32WB55) and the internal
CC1101 radio (device name `cc1101_int`). External CC1101 modules are reached
through the same portable API but are not yet surfaced in the menus.
