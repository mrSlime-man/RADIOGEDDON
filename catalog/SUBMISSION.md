# Apps Catalog submission (RadioGeddon 1.0, Catalog edition)

Status: **prepared, not submitted.** Everything the catalog's bundler checks
passes except screenshots, which must be genuine qFlipper screenshots from a
Flipper Zero running the Catalog edition on Official firmware
([screenshots/README.md](screenshots/README.md)). Nothing here is listed in
the Apps Catalog or Flipper Lab yet.

## What is ready

- `application.fam`: appid `radiogeddon`, name `RadioGeddon`, category
  `Sub-GHz`, `fap_version` `1.0`, author, 10x10 1-bit icon, Catalog
  edition define.
- [manifest.template.yml](manifest.template.yml): the catalog schema
  (`sourcecode` with `type: git`, `origin`, full `commit_sha`;
  `short_description`, `description`, `changelog`, `screenshots`).
- [description.md](description.md) and [changelog.md](changelog.md) in the
  catalog's Markdown subset (checked with the catalog's own filter).
- CI runs `scripts/check_catalog.py --catalog` and the catalog's
  `tools/bundle.py` steps (`scripts/catalog_bundle.py`) on every pull
  request: clone, lint, build, manifest, path, includes and values pass.

## Steps left

1. On Official firmware 1.5.1, install `radiogeddon-catalog-official.fap`
   from the v1.0.1 release (built against the 1.5.1 SDK, API 88.2; v1.0.0's
   file is refused there as "Outdated app") and take 2-6 screenshots with qFlipper's screenshot
   button. Commit them unedited as `catalog/screenshots/ss0.png`, `ss1.png`,
   ... (`ss0.png` is the preview).
2. `python3 scripts/check_catalog.py --submission` must pass.
3. `python3 scripts/check_catalog.py --write-manifest manifest.yml --commit <full SHA of that commit on main>`.
4. Fork flipperdevices/flipper-application-catalog, add the file as
   `applications/Sub-GHz/radiogeddon/manifest.yml`, and run
   `python3 tools/bundle.py --nolint applications/Sub-GHz/radiogeddon/manifest.yml bundle.zip`.
5. Open the pull request with the body below. Keep the Reviewer Checklist.

The commit with the screenshots is newer than the `v1.0.1` tag; its
application source is the same as v1.0.1's as long as only
`catalog/screenshots/` changed in between.

## Pull request body (draft)

```markdown
# Application Submission

- RadioGeddon 1.0 (Catalog edition): a standalone Sub-GHz research and signal analysis toolkit for the Official firmware. Scanner and frequency hopper, receive and protocol identification with the firmware's own decoders, streaming RAW recording to the SD card, Decode with Firmware, signal analysis (timing, encoding, frame structure, field map, labelled as confirmed/measured/inferred), pulse timeline, comparison, signal database, recording recovery, memory diagnostics and replay.
- Transmission: every transmission is gated on furi_hal_region_is_frequency_allowed() and furi_hal_subghz_is_frequency_valid() (helpers/radiogeddon_subghz.c, radiogeddon_subghz_tx_check); a refusal is explained on screen. Decoded rolling-code protocols are never replayed. No key recovery or decryption.
- Files: writes only under /ext/apps_data/radiogeddon. Recordings are standard .sub files.
- Source: https://github.com/mrSlime-man/RADIOGEDDON (MIT), release v1.0.1.

# Extra Requirements

- None. An external CC1101 module is optional.

# Author Checklist (Fill this out)

- [x] I've read the [contribution guidelines](../blob/HEAD/documentation/Contributing.md) and my PR follows them
- [x] I own the code I'm submitting or have code owner's permission to submit it
- [x] I have performed a self-review of my own code
- [x] I have commented my code, particularly in hard-to-understand areas
- [x] I [have validated](../blob/HEAD/documentation/Contributing.md#validating-manifest) the manifest file(s) with `python3 tools/bundle.py --nolint applications/CATEGORY/APPID/manifest.yml bundle.zip`

# AI usage disclosure (Fill this out)

- Fully AI generated - explain what all the generated code does in moderate detail.

- The application code, host tests and CI were written with Claude Code (Anthropic) under the owner's direction, review and on-device testing. The app is a Sub-GHz scanner, hopper, receiver/recorder and offline analysis toolkit: radio access goes through the firmware's subghz_devices API and decoders; the analysis engines (helpers/rg_*.c) are pure C over recorded pulse timings and are host-tested (ASan/UBSan, fuzzing); the screens are standard SceneManager/ViewDispatcher scenes and custom views. Hardware testing so far: the Full edition of the same code on RogueMaster (launch, exit, scanner); the Catalog edition was run on Official firmware for these screenshots.

# Reviewer Checklist (Don't fill this out, and don't remove it from the template)

- [ ] Bundle is valid
- [ ] There are no obvious issues with the source code
- [ ] I've ran this application and verified its functionality
```

Before opening it, check the AI disclosure option against how the code was
actually written (keep exactly one option), and adjust the hardware
sentence to what was really run on Official firmware.
