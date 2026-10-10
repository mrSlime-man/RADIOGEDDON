# Contributing to RadioGeddon

Thanks for your interest in RadioGeddon! This guide covers how to report
results, build and test the app, and get a change merged.

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## What we welcome — and what we don't

RadioGeddon is an **analysis and authorized-testing** tool. Contributions that
improve signal capture, analysis quality, usability, reliability,
documentation, and firmware compatibility are very welcome.

We will not accept features whose purpose is to defeat security: key recovery,
rolling-code prediction or bypass, brute forcing, jamming, or weakening the
transmit safeguards (regional limits, the refusal to replay dynamic/rolling
protocols). See [SECURITY.md](SECURITY.md) for the responsible-use policy.

## The most valuable contribution right now: hardware testing

The app builds as two editions for four firmware families and passes all automated checks, but
**its on-device behaviour has not yet been verified on a physical Flipper
Zero**. If you have one:

1. Install the `.fap` for your firmware ([Installation](docs/INSTALLATION.md)).
2. Work through any part of the [hardware checklist](docs/HARDWARE_CHECKLIST.md).
3. Report results — passes *and* failures — with the
   [Hardware test report](https://github.com/mrSlime-man/RADIOGEDDON/issues/new?template=hardware_report.yml)
   issue form.

Only transmit with devices you own or are authorized to test, within your
region's rules.

## Reporting bugs and requesting features

Use the [issue forms](https://github.com/mrSlime-man/RADIOGEDDON/issues/new/choose).
Please include your firmware family and version, the RadioGeddon version from
the About screen, and which `.fap` you installed. Security problems go through
[SECURITY.md](SECURITY.md), not public issues.

## Development setup

Requirements: Linux or macOS, `git`, `python3` (3.9+), `curl`, a host C
compiler for the unit tests, and [uFBT](https://pypi.org/project/ufbt/) at the
pinned version:

```bash
git clone https://github.com/mrSlime-man/RADIOGEDDON.git
cd RADIOGEDDON
source scripts/firmware_pins.sh && pip install "ufbt==${UFBT_VERSION}"
```

### Build

`scripts/build_target.sh` is the exact build used by CI and releases. It
writes the edition's copy of the source, downloads the SDK pinned in
[`scripts/firmware_pins.sh`](scripts/firmware_pins.sh), checks its SHA-256 and
API version, builds, and verifies the `.fap` manifest and its imports:

```bash
scripts/build_target.sh catalog-official   # -> dist/release/radiogeddon-catalog-official.fap
scripts/build_target.sh full-momentum      # -> dist/release/radiogeddon-full-momentum.fap
scripts/build_target.sh full-unleashed     # -> dist/release/radiogeddon-full-unleashed.fap
scripts/build_target.sh full-roguemaster   # clones RogueMaster at the pinned commit (large)
scripts/build_release.sh                   # all four + SHA256SUMS + BUILD_INFO.txt
```

For a fast edit–build–run loop on a connected Flipper running **official**
firmware, reuse the SDK the script deployed:

```bash
UFBT_HOME=$PWD/.ufbt-official ufbt            # build
UFBT_HOME=$PWD/.ufbt-official ufbt launch     # install and start on the device
```

`ufbt launch` only works when the deployed SDK matches the firmware on the
device. That builds the Catalog edition; for the Full edition on a Momentum or
Unleashed device, stage it first:

```bash
python3 scripts/stage_edition.py full .stage/dev && cd .stage/dev
UFBT_HOME=$PWD/../../.ufbt-momentum ufbt launch    # or .ufbt-unleashed
```

### Editions

Edition differences are feature switches in
[`radiogeddon_edition.h`](radiogeddon_edition.h) (`RG_FEATURE_*`). Guard
edition-specific code with the switch it belongs to, never with the edition
itself, and keep shared code (settings, storage, radio, engines) identical in
both editions, so files written by one stay readable — and are not damaged —
by the other. A new switch needs a check in `test/test_edition.c`.

### Test

```bash
make -C test check                    # host tests + fuzz corpus, -Werror, ASan + UBSan
make -C test formats                  # firmware FlipperFormat code + its .sub test files
make -C test decoders                 # firmware Sub-GHz decoders on its RAW captures, and its presets
make -C test captures                 # the analyzer on those captures, scored against the decoders
make -C test lifecycle                # Scanner / Range Scanner / Hopper start-stop cycles, allocations counted
python3 scripts/check_catalog.py      # the Catalog edition against the Apps Catalog rules
make -C test fuzz FUZZ_TIME=60        # fuzz each target under libFuzzer (needs clang)
python3 scripts/check_links.py        # Markdown links and anchors
UFBT_HOME=$PWD/.ufbt-official ufbt lint     # clang-format check
UFBT_HOME=$PWD/.ufbt-official ufbt format   # apply formatting
UFBT_HOME=$PWD/.ufbt-official python3 scripts/static_analysis.py  # GCC -fanalyzer + clang-tidy (Catalog)
UFBT_HOME=$PWD/.ufbt-momentum python3 scripts/static_analysis.py --root .stage/full-momentum  # (Full, after building it)
```

The host tests cover the firmware-independent code (`helpers/rg_*` and
`helpers/radiogeddon_dsp.*`) and, through small Furi/Storage stand-ins in
`test/stubs/`, the recorder and the Database loader. If you change or add such
logic, add a test in `test/` and keep that code free of firmware headers so it
stays host-testable. `make -C test formats` builds the firmware's own
FlipperFormat and stream code with the settings and `.sub` loading code and
runs them on the firmware's Sub-GHz test files; it downloads those files
(GPL-3.0, checked against `test/firmware/files.sha256`, never committed) on
first use. `make -C test decoders` does the same with the firmware's Sub-GHz
decoders and its RAW captures, fed through `rg_decode`, and `make -C test
captures` scores the analyzer on those captures against the decoders'
timing; a change to `rg_analyzer.c` must not lower its totals. Code that reads files
from the SD card should also get a fuzz target (`test/fuzz/`); an input that
once broke it goes into `test/fuzz/corpus/<target>/` so every test run replays
it. The static analysis checks are listed, with the reasons for the ones
turned off, in [`.clang-tidy`](.clang-tidy).

CI cannot exercise the radio. If you tested on hardware, say so (and on which
firmware) in your pull request; if you did not, say that too.

## Code guidelines

- Follow the existing style; `ufbt lint` must pass (configuration in
  [`.clang-format`](.clang-format)).
- All radio access goes through `helpers/radiogeddon_subghz.*`. Keep RX/TX
  sequencing consistent with the firmware's Sub-GHz subsystem.
- Radio worker callbacks run off the GUI thread: record data under the existing
  mutex and post a custom event; never touch views from the worker thread.
- Label analysis output honestly. `[CONFIRMED]` is reserved for results backed
  by a firmware protocol decoder; direct measurements of a capture are
  `[OBSERVED]`; statistics-based guesses are `[HEURISTIC]`; engine inferences
  are `[HYPOTHESIS]`. Never present an inference as a verified
  decode.
- Keep transmit safeguards intact: firmware region checks, refusing
  dynamic/rolling-code protocols, and refusing unknown modulation presets.
- Prefer small, focused pull requests.

## Pull requests

1. Fork and create a branch from `main`.
2. Make your change with tests and documentation updates.
3. Add an entry under **Unreleased** in [CHANGELOG.md](CHANGELOG.md).
4. Open a pull request; the template's checklist explains what reviewers look
   for. CI (links, host tests, fuzzing, static analysis,
   Official/Unleashed/RogueMaster builds with API and manifest verification,
   lint) must pass.

## Releasing (maintainers)

1. Update the version in [`radiogeddon_version.h`](radiogeddon_version.h) and,
   if MAJOR.MINOR changed, `fap_version` in `application.fam`.
2. Add release notes at `docs/releases/vX.Y.Z[-pre].md` (first line is the
   release title) and move CHANGELOG entries out of **Unreleased**.
3. Update the download links in `README.md` and `docs/INSTALLATION.md`.
4. Run `python3 scripts/release_meta.py`. It checks that the version, notes,
   CHANGELOG section, the API listed for each `.fap` (notes and
   `docs/FIRMWARE_COMPATIBILITY.md`) and the download links all agree. CI runs
   it on every pull request, and the Release workflow refuses a tag that fails
   it.
5. Merge to `main`, then run **Actions → Release → Run workflow** with
   `dry_run` enabled. It builds everything, stages a private draft, verifies the
   uploaded assets and deletes the draft.
6. Publish: run the workflow again with `dry_run` off (it creates the tag on
   the branch head), or tag and push:
   `git tag -a vX.Y.Z -m "RadioGeddon vX.Y.Z" && git push origin vX.Y.Z`.
   The Release workflow rebuilds, attests and verifies build provenance, and
   publishes (as a pre-release for suffixed versions such as `-beta.1`).

The **Firmware watch** workflow runs weekly (and by hand from Actions). It
lists newer Official, Unleashed and RogueMaster versions with their API
versions and builds the app against every newer Official or Unleashed SDK,
including Official's release candidate; it fails only if one no longer builds
the app. Locally: `python3 scripts/firmware_watch.py --build`.

To move to newer firmware, update the URL, SHA-256 and API values in
`scripts/firmware_pins.sh`, rebuild, run the hardware checklist on that
firmware, and record the change in the changelog.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).
