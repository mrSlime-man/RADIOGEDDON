# Installing RadioGeddon

RadioGeddon is a single `.fap` file. Pick the build that matches your firmware
(see [FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md)) and copy it onto
the Flipper.

## What you need

- A Flipper Zero running official, Unleashed, or RogueMaster firmware.
- The matching `.fap` from `dist/release/`:

  | Firmware | File |
  |----------|------|
  | Official | `dist/release/official/radiogeddon-official.fap` |
  | Unleashed | `dist/release/unleashed/radiogeddon-unleashed.fap` |
  | RogueMaster | `dist/release/roguemaster/radiogeddon-roguemaster.fap` |

  Installing the wrong one triggers an API-version mismatch — pick the build
  for your firmware. Checksums are in `dist/release/SHA256SUMS`.

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

- RadioGeddon stores its recordings as standard `.sub` files under
  `/ext/apps_data/radiogeddon/signals` (created on first run), keeping them
  organised separately from the stock Sub-GHz library while remaining fully
  `.sub`-compatible. The **Database** screen browses that folder; you can also
  copy stock `/ext/subghz` captures there to analyze or replay them.
- No extra assets are required. Manufacturer keystores (for identifying some
  KeeLoq-family protocols) are loaded best-effort from the firmware's existing
  `/ext/subghz/assets` if present; if missing, those specific protocols simply
  won't fully identify — everything else still works.

## Verifying the download

Checksums for the released artifacts are in `dist/release/SHA256SUMS`:

```bash
cd dist/release && sha256sum -c SHA256SUMS
```

## Uninstalling

Delete the `.fap` from `apps/Sub-GHz/`. Your recordings under
`/ext/apps_data/radiogeddon/signals` are left untouched; delete that folder too
if you want them gone.
