# Troubleshooting

Problems are grouped by where they show up. If none of this helps, please
[open an issue](https://github.com/mrSlime-man/RADIOGEDDON/issues/new/choose)
with your firmware family and version, the RadioGeddon version from the About
screen, the exact on-screen text, and the steps to reproduce.

## The app won't open

The Flipper checks every app against its firmware before running it. These
messages come from the firmware (text from the official firmware; forks may
word them slightly differently):

| Message | Cause | Fix |
|---------|-------|-----|
| `Error: Outdated App` — *Update the app* | The `.fap` was built for an **older API major version** than your firmware — usually `radiogeddon-official.fap` on Unleashed or RogueMaster. | Install the file for your firmware family. |
| `Error: Outdated Firmware` — *Update firmware* | The `.fap` was built for a **newer API major version** — usually the Unleashed or RogueMaster file on official firmware. | Install the file for your firmware family. |
| `Error: Missing Imports` | Right family, but your firmware is older than the build expects (lower API minor version), or the file is from another fork. | Update your firmware, or build from source against your firmware's SDK. |
| `Error: HW Target Mismatch` | Not a Flipper Zero `f7` build. | Download a release `.fap` again. |
| `Error: Invalid File` / `Invalid Manifest` | The file is damaged or incomplete. | Download it again and check `sha256sum -c SHA256SUMS`. |

How to tell which firmware you have, and what each build supports:
[Firmware Compatibility](FIRMWARE_COMPATIBILITY.md). Never edit a `.fap` to
change its API version — that disables a safety check and can crash the device.

## The app isn't in the Apps menu

The file must be in a folder the launcher scans, normally `apps/Sub-GHz/` on the
SD card, and must end in `.fap`. Re-copy it with qFlipper
([Installation](INSTALLATION.md)) and restart the Flipper if it still doesn't
appear.

## `No radio` — `Sub-GHz device not found or not responding.`

The Scanner, Receive & Record or Frequency Hopper couldn't access the CC1101
radio. Press **Back** and try again. If it persists, reboot the Flipper (**Settings → Power → Reboot**, or hold
**Left + Back**). If it still happens after a reboot, please report it with a
device log — this is exactly the kind of result the
[hardware checklist](HARDWARE_CHECKLIST.md) needs.

## Nothing is decoded while receiving

- **Wrong frequency.** Use the **Scanner** while pressing the remote and pick the
  frequency whose bar jumps, then press **OK** to receive there.
- **Wrong modulation.** Most remotes are `AM 650`; some sensors need `AM 270`;
  FSK devices need an `FM` preset (**Settings → Modulation**).
- **Unknown protocol.** Only protocols your firmware can decode are recognised.
  Press **Left** to record RAW instead, then use *Unknown Protocol Analysis*
  from the **Database**.
- **Rolling-code manufacturer names missing.** Some KeeLoq-family protocols are
  only fully identified when the firmware's manufacturer keystore is on the SD
  card.

## RSSI stays at the bottom

Make sure the transmitter is actually sending (many remotes only transmit while
the button is held) and is within a few metres. The bar's scale runs from
−100 dBm (empty) to −30 dBm (full); background noise usually sits near the
bottom.

## A recording shows `lost <n>`

The SD card could not keep up with the incoming pulses, so some were dropped.
This happens mostly with very noisy input (a strong interferer, or AM with no
signal) on a slow or nearly full card. The saved file still opens; it notes the
loss, and the timing jumps where samples are missing. Record again closer to
the transmitter, stop soon after the transmission, or try a faster card.
`buf <n>%` on the same line is the write buffer filling up, an early warning.

## Recording doesn't start (red LED blinks)

The hint line says why:

- `REC: Not enough memory` — there was no room for even the smallest write
  buffer. Close other apps or restart the Flipper, then try again.
- `REC: Cannot create file` — the SD card is missing, read-only or full.

## `SD card write failed` while recording

The card refused a write (removed, full or faulty), so the recording stopped
and was not kept. Check the card and record again.

## Saving fails (error tone)

- An SD card must be inserted, writable, and have free space.
- If the card was replaced or reformatted while the app was open, restart the app
  so it can recreate `/ext/apps_data/radiogeddon/signals`.
- Check the card with **Settings → Storage** on the Flipper, or in qFlipper.

## A file shows `BAD` in the Database

The file couldn't be read as a Flipper Sub-GHz `.sub` file, for example a
truncated copy, a file with a missing header, or another kind of file renamed
to `.sub`. It opens with only `File details`, `Rename` and `Delete`;
`File details` shows how its first line starts. Valid files start with
`Filetype: Flipper SubGhz Key File` or `Filetype: Flipper SubGhz RAW File`.

## Rename says `Name already used`

Another file in the signals folder has that name. The SD card ignores letter
case, so `Gate` and `gate` are the same name: rename to something else first,
then to the new spelling.

## Replay shows an error

| Message | What to do |
|---------|-----------|
| `Blocked by region` | Your Flipper's region settings forbid transmitting on this frequency. This is enforced by the firmware; only transmit where you are legally allowed to. |
| `Protected/rolling code` | Rolling-code (dynamic) protocols and protocols the firmware can't encode are never replayed. This is intentional. |
| `Unsupported file` | The file couldn't be read, or its modulation preset isn't one RadioGeddon recognises (standard AM/FM presets or a stored custom register set). |
| `File not found` | The file was moved or deleted; reopen it from the Database. |
| `No radio device` / `Radio busy` | Go back, wait a moment and try again; reboot if it persists. |

## Replay said `Signal sent` but the device didn't react

- The device may use a rolling code captured as RAW — a replayed press has
  already been used and is rejected by the receiver. That is how rolling codes
  are meant to work.
- The capture may be weak or incomplete: re-record closer to the transmitter and
  check *Signal Info & Analysis* for a clean, regular timing pattern.
- Some receivers need the button held for longer than a single replayed burst.

## Compare gives a low score for the same button

The RAW timing match compares two captures sample by sample from the start
without aligning them. If one recording started earlier (more noise before the
signal) or caught a different part of the transmission, the score drops even for
the same button. Start both recordings the same way — just before pressing the
button — or compare decoded files instead.

## *Unknown Protocol Analysis* shows `0` changing bits for a rolling-code remote

The field map compares frames within one recording, and a single press usually
repeats the same frame. Record **several presses** in one RAW capture to see
which bits change between presses.

## *Unknown Protocol Analysis* says *Not enough free memory*

The analysis needs about 8 KB of contiguous free heap plus a safety margin and
checks before it starts rather than risk a crash. Go back to the main menu
(which frees the scanner and hopper), close other apps, and try again.

## *Unknown Protocol Analysis* says *No frame fits PWM, PPM or Manchester*

The capture has timing peaks but no frame decodes cleanly in any of the three
encodings. Common reasons: the capture is mostly noise (check the `Noise`
percentage and the frame list), the signal is FSK rather than on-off keyed, or
it uses more than two symbol widths. Record closer to the transmitter, and with
the right modulation in **Settings**.

## Collecting a device log

1. Connect the Flipper over USB.
2. Open a CLI session: `ufbt cli` (or the CLI in qFlipper), then type `log`.
3. Reproduce the problem and copy the output into your issue.
