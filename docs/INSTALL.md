# Installing RadioGeddon

RadioGeddon is a single `.fap` file. Pick the build that matches your firmware
(see [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md)) and copy it onto
the Flipper.

## What you need

- A Flipper Zero running official, Unleashed, or RogueMaster firmware.
- The matching `.fap` from `dist/release/` (or a GitHub Actions build artifact,
  or one you built yourself).

## Option A — qFlipper (desktop, easiest)

1. Connect the Flipper over USB and open **qFlipper**.
2. Go to the **File manager**.
3. Navigate to `SD Card/apps/Sub-GHz/`.
4. Drag the matching `.fap` (e.g. `radiogeddon-official.fap`) into that folder.
5. On the Flipper: **Apps → Sub-GHz → RadioGeddon**.

## Option B — SD card directly

1. Power off the Flipper, remove the microSD, insert it into your computer.
2. Copy the `.fap` to `apps/Sub-GHz/` on the card.
3. Reinsert the card and power on.
4. **Apps → Sub-GHz → RadioGeddon**.

## Option C — ufbt launch (for developers)

With the Flipper connected over USB:

```bash
ufbt launch        # builds against the currently-deployed SDK and runs it
```

Make sure the SDK you deployed with `ufbt update` matches your firmware.

## First run

- RadioGeddon reads and writes recordings in the standard `/ext/subghz`
  folder, so they are shared with the stock Sub-GHz app.
- No extra assets are required. Manufacturer keystores (for decoding some
  rolling-code protocols) are loaded from the firmware's existing
  `/ext/subghz/assets` if present; if they are missing, those specific
  protocols simply won't fully decode — everything else still works.

## Verifying the download

Checksums for the released artifacts are in `dist/release/SHA256SUMS`:

```bash
cd dist/release && sha256sum -c SHA256SUMS
```

## Uninstalling

Delete the `.fap` from `apps/Sub-GHz/`. RadioGeddon stores no other files of
its own; your recordings in `/ext/subghz` are left untouched.
