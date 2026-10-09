# Firmware Compatibility

RadioGeddon is a **standalone external application (`.fap`)** for the Flipper
Zero. It uses only public SDK APIs — chiefly the portable `subghz_devices`
radio layer and the `subghz` protocol library — which are present in official
firmware and in the Unleashed / RogueMaster family. **No firmware patches or
custom firmware features are required.**

## The key fact: the API version gate

Every Flipper firmware SDK publishes an **API version** (e.g. `87.1`). When a
`.fap` is built, that number is embedded in the file's metadata. At load time
the firmware refuses any `.fap` whose embedded API is incompatible with the
running firmware. This is a safety mechanism: an ABI mismatch would otherwise
crash the device.

Because official and Unleashed/RogueMaster track **different API versions**, a
single `.fap` will generally not load on both. The correct, supported solution
is to ship **one `.fap` per firmware family, each built against that family's
own SDK**.

> **We never edit `application.fam` or the compiled metadata to fake an API
> version so a mismatched build will load.** Doing so bypasses the ABI safety
> check and causes crashes or undefined behaviour on real hardware. If your
> firmware rejects a build, install the build that matches it (or build from
> source against your SDK) — do not force it.

## Verified builds

The identical source tree compiles **without modification** against:

| Firmware family        | SDK used to verify        | Embedded API | Status |
|------------------------|---------------------------|--------------|--------|
| Official (stable)      | `release` channel, 1.4.3  | `87.1`       | ✅ Builds clean, APPCHK passes |
| Unleashed              | `unlshd-093`              | `88.9`       | ✅ Builds clean, APPCHK passes |
| RogueMaster            | RogueMaster SDK           | (Unleashed-based) | ⚠️ Source-compatible; build against the RM SDK (see below). Not built in CI yet because a public SDK index URL is needed. |

RogueMaster is built on top of Unleashed and exposes the same Sub-GHz API
surface this app uses, so it is source-compatible. We mark it ⚠️ only because
our automated pipeline has not yet fetched an RM SDK to produce a verified
artifact — not because of any known incompatibility.

> **Hardware note:** "Builds clean / APPCHK passes" means the compiler, linker
> and the SDK's API-compatibility check all succeed. On-device behaviour has
> **not** yet been verified on physical hardware — see
> [HARDWARE_TEST_PLAN.md](HARDWARE_TEST_PLAN.md).

## Which file do I install?

1. Find your firmware's channel: official, Unleashed, or RogueMaster.
2. Install the matching artifact:
   - `radiogeddon-official.fap` → official firmware
   - `radiogeddon-unleashed.fap` → Unleashed
   - RogueMaster → build from source against the RM SDK (below), or use the
     Unleashed build only if your RM release reports the same API version.
3. If the Flipper shows *"API version mismatch"* (or the app fails to open),
   you installed the wrong build — install the one matching your firmware, or
   rebuild from source.

## Building for your exact firmware (recommended)

The most reliable approach is to build against the SDK of the firmware you
actually run, using [`ufbt`](https://pypi.org/project/ufbt/):

```bash
pip install ufbt

# Official (stable):
ufbt update --channel release
ufbt                      # -> dist/radiogeddon.fap

# Unleashed:
UFBT_HOME=.ufbt-unleashed ufbt update --index-url https://up.unleashedflip.com/directory.json
UFBT_HOME=.ufbt-unleashed ufbt

# RogueMaster: point --index-url at your RM SDK directory.json, or build the
# app inside a RogueMaster firmware checkout with its own fbt:
#   cp -r <this repo> applications_user/radiogeddon
#   ./fbt fap_radiogeddon
```

`scripts/build_release.sh` automates the official + Unleashed builds and writes
named artifacts plus `SHA256SUMS` into `dist/release/`.

## Hardware target

Builds target the Flipper Zero's `f7` hardware (STM32WB55) and the internal
CC1101 radio. External CC1101 modules are not required and are not currently
selected by the app (it uses the internal radio by name, `cc1101_int`).
