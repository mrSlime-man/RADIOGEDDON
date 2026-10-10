# Installation

Installing RadioGeddon takes about two minutes: pick the file for your
firmware, copy it to the Flipper, and open it from the Apps menu.

## What you need

- A Flipper Zero with a microSD card.
- One of these firmware versions (or a newer release of the same family):

  | Firmware family | Tested build | API |
  |-----------------|--------------|-----|
  | Official | 1.4.3 | 87.1 |
  | Unleashed | unlshd-093 | 88.9 |
  | RogueMaster | commit `38d7ae9` | 88.16 |

- A computer with [qFlipper](https://flipperzero.one/update) (Windows, macOS,
  Linux) — or a microSD card reader.

## Step 1 — Find out which firmware you have

On the Flipper, open **Settings → About** (or look at the firmware version in
qFlipper):

| You see | Your firmware | File to download |
|---------|---------------|------------------|
| A plain version number such as `1.4.3` | Official | `radiogeddon-official.fap` |
| A version starting with `unlshd-` | Unleashed | `radiogeddon-unleashed.fap` |
| A RogueMaster version | RogueMaster | `radiogeddon-roguemaster.fap` |

Using a different fork? See [Firmware Compatibility](FIRMWARE_COMPATIBILITY.md).

## Step 2 — Download

From the [**v1.0.0-beta.3 release page**](https://github.com/mrSlime-man/RADIOGEDDON/releases/tag/v1.0.0-beta.3),
or directly:

- [radiogeddon-official.fap](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0-beta.3/radiogeddon-official.fap)
- [radiogeddon-unleashed.fap](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0-beta.3/radiogeddon-unleashed.fap)
- [radiogeddon-roguemaster.fap](https://github.com/mrSlime-man/RADIOGEDDON/releases/download/v1.0.0-beta.3/radiogeddon-roguemaster.fap)

### Optional: verify the download

Download `SHA256SUMS` from the same release into the same folder and run:

```bash
sha256sum -c --ignore-missing SHA256SUMS      # Linux: prints "radiogeddon-...fap: OK"
```

On macOS run `shasum -a 256 radiogeddon-official.fap`, and on Windows
`Get-FileHash radiogeddon-official.fap -Algorithm SHA256` in PowerShell; the
printed hash must match the file's line in `SHA256SUMS`.

Each `.fap` also has a signed build-provenance attestation. With the GitHub CLI
installed you can confirm it was built by this repository's release workflow
from the tagged source:

```bash
gh attestation verify radiogeddon-official.fap --repo mrSlime-man/RADIOGEDDON
```

## Step 3 — Copy it to the Flipper

### With qFlipper (recommended)

1. Connect the Flipper to your computer with a USB cable and open **qFlipper**.
2. Wait until qFlipper shows your device, then open the **File manager** tab.
3. Open **SD Card → apps → Sub-GHz**.
4. Drag the `.fap` file you downloaded into that folder.
   (Updating? Replace the old file when asked.)
5. Disconnect qFlipper if it keeps the Flipper busy.

### With a card reader

1. Turn the Flipper off and remove the microSD card.
2. Insert it into your computer and copy the `.fap` to `apps/Sub-GHz/`.
3. Put the card back and turn the Flipper on.

## Step 4 — Open RadioGeddon

On the Flipper: **Apps → Sub-GHz → RadioGeddon**.

On first launch RadioGeddon creates `/ext/apps_data/radiogeddon/signals` on the
SD card for your recordings. Continue with the [User Guide](USER_GUIDE.md).

If the Flipper shows an error such as `Outdated App`, `Outdated Firmware` or
`Missing Imports` instead, you installed the file for a different firmware
family — see [Troubleshooting](TROUBLESHOOTING.md#the-app-wont-open).

## Optional: reference recordings

The repository's [`test/fixtures`](../test/fixtures) folder holds three small
synthetic `.sub` files (two Princeton codes and one RAW capture). Copy them to
`SD Card/apps_data/radiogeddon/signals/` to try the Database, analysis and
compare screens without capturing anything.

## Updating

Download the new `.fap` for your firmware and replace the old file in
`apps/Sub-GHz/`. Your recordings are kept. Whenever you update your Flipper's
firmware, check whether a matching RadioGeddon build is needed
([Firmware Compatibility](FIRMWARE_COMPATIBILITY.md)).

## Uninstalling

Delete `apps/Sub-GHz/radiogeddon-*.fap`. Your recordings stay in
`apps_data/radiogeddon/signals` until you delete that folder too.

## Building from source instead

See [CONTRIBUTING.md](../CONTRIBUTING.md#development-setup) — one command builds
and verifies the `.fap` for each firmware family.
