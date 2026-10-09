# Troubleshooting

## The app won't open / "API version mismatch"

You installed a build that doesn't match your firmware. The `.fap` embeds the
SDK API version it was built against, and the firmware refuses incompatible
builds on purpose. Install the build matching your firmware family, or rebuild
from source against your SDK. See
[FIRMWARE_COMPATIBILITY.md](FIRMWARE_COMPATIBILITY.md). **Do not** try to edit
the file's metadata to force it — that bypasses a safety check and will crash.

## "Radio not available" on the Receiver / Record / Hopper screen

The app could not initialise the internal CC1101 radio. Usually this means the
app is running somewhere without radio hardware (an emulator), or the radio is
busy. Press **Back** to leave the screen. If it persists on real hardware,
reboot the Flipper (**Settings → System → Reboot**) and try again.

## "RX start failed"

The selected frequency isn't valid for the radio, or RX couldn't start. Open
**Settings** and choose a standard frequency (e.g. 433.92). If it still fails,
reboot the Flipper.

## "SD write failed" when recording

The recording could not be opened on the SD card. Check that:
- an SD card is inserted and not write-protected;
- the card has free space;
- the card is formatted correctly (the Flipper uses FAT). Re-seat or reformat
  the card via **Settings → Storage** if needed.

## Recording shows "No samples"

No RF activity was captured on the current frequency/modulation before you
pressed Stop. Make sure you are on the right frequency and modulation for your
device (**Settings**), keep the remote close, and press the remote's button
*during* recording. OOK remotes are usually `AM650`; many sensors use `AM270`.

## Replay says "TX blocked by region"

The firmware's region policy does not allow transmitting on that frequency.
This is enforced by the firmware, not by RadioGeddon, and is expected on
official firmware for frequencies outside your region's allowed bands. Only
transmit where you are legally authorized.

## Replay says "Not a RAW recording" / "Unsupported preset"

RadioGeddon's replay streams **RAW** `.sub` recordings. It can replay:
- recordings made by RadioGeddon itself, and
- other RAW `.sub` files that use a standard preset (AM270/AM650/FM238/FM476).

Decoded-protocol `.sub` files (non-RAW) and files with a custom preset are not
replayed by this app — open them in the stock Sub-GHz app instead.

## Replay "Sent" but the device didn't react

Rolling-code remotes (garage doors, modern car keys) intentionally cannot be
replayed: each press uses a new code. RadioGeddon flags these as
`(rolling)` in the Receiver. A one-to-one replay only works for fixed-code
devices, and only where you are authorized to transmit.

## Menus feel sluggish while receiving

Live screens refresh a few times per second and decode on a background worker,
so navigation stays responsive. If you still see lag, leave the live screen
(press Back) before browsing saved files — only one radio operation runs at a
time by design.

## Compare always says "DIFFERENT"

Comparison works on RAW recordings. Make sure both files are RAW `.sub`
captures (the info screen shows `Protocol: RAW`). Captures of the *same* button
taken at different times should score high; different buttons or noisy captures
score low. Rolling-code captures will differ every time by design.

## Where are my recordings?

In `/ext/subghz` on the SD card, as standard `.sub` files, shared with the
stock Sub-GHz app. You can manage them there or from **Saved Signals** in the
app.

## Reporting a bug

Open an issue at <https://github.com/mrslime-man/radiogeddon> and include:
your firmware name and version, which build of RadioGeddon you installed, the
exact on-screen message, and the steps to reproduce.
