# User Guide

This guide walks through every RadioGeddon screen. Text in `code` style is
exactly what appears on the Flipper.

> RadioGeddon is in public beta and its on-device behaviour has not yet been
> verified on physical hardware. If something doesn't behave as described here,
> please [report it](https://github.com/mrSlime-man/RADIOGEDDON/issues/new/choose).

## Starting the app

On the Flipper: **Apps → Sub-GHz → RadioGeddon**. (Not installed yet? See
[Installation](INSTALLATION.md).)

Buttons work the same way everywhere:

| Button | Action |
|--------|--------|
| **Up / Down** | Move through menus, lists and reports |
| **OK** | Select / confirm the highlighted action |
| **Left / Right** | Change a setting's value; on live receive, **Left** starts/stops RAW recording |
| **Back** | Return to the previous screen (stops any radio activity of the screen you leave) |

## Main menu

| Item | What it does |
|------|--------------|
| `Scanner` | Live RSSI sweep across common frequencies |
| `Receive & Record` | Live decoding of known protocols and RAW recording on one frequency |
| `Frequency Hopper` | Live decoding while hopping across a list of frequencies |
| `Database` | Browse, analyse, compare, replay and delete saved recordings |
| `Settings` | Choose the frequency and modulation |
| `About` | Version, modules, label legend and storage location |

## Settings

| Setting | Values | Default |
|---------|--------|---------|
| `Frequency MHz` | 300.00, 303.87, 304.25, 310.00, 315.00, 318.00, 390.00, 418.00, 433.07, 433.42, **433.92**, 434.42, 434.77, 438.90, 464.00, 779.00, 868.35, 915.00, 925.00 | 433.92 |
| `Modulation` | `AM 270`, **`AM 650`**, `FM 2.38k`, `FM 47.6k` | AM 650 |
| `Threshold` | `+6`, `+8`, **`+10`**, `+15`, `+20`, `+30` dB over the noise floor | +10 dB |
| `Scan list` | **`All`**, `300-348`, `387-464`, `779-928`, `Custom` | All |
| `Edit scan list` | Press **OK** to switch individual frequencies on or off | all on |
| `Scan dwell` | `5`, **`10`**, `20`, `50`, `100` ms per frequency | 10 ms |
| `Hold on hit` | **`Off`**, `On` | Off |
| `Hop list` | **`Common`** (315 / 390 / 433.92 / 868.35), `All`, `300-348`, `387-464`, `779-928`, `Custom` | Common |
| `Edit hop list` | Press **OK** to switch individual frequencies on or off | Common four |
| `Hop dwell` | `100`, **`200`**, `300`, `500`, `1000` ms per frequency | 200 ms |
| `Activity hold` | `1`, **`2`**, `3`, `5`, `10` s after activity was last seen | 2 s |
| `Hop auto-rec` | **`Off`**, `On` | Off |

Use **Left / Right** to change a value and **Back** to apply it. The
frequency is used by *Receive & Record*; the modulation is used by the
*Scanner*, *Receive & Record* and the *Frequency Hopper* (*Replay* always uses
the recording's own modulation). `Threshold` applies to both the *Scanner* and
the *Frequency Hopper*; the `Scan` settings apply to the Scanner and the `Hop`
settings and `Activity hold` to the Hopper.

Settings are saved to `apps_data/radiogeddon/settings.txt` on the SD card when
you leave the Settings screen, and are restored the next time the app starts.
A missing or damaged file just means the defaults are used.

Most remotes and doorbells use `AM 650` (on-off keying). Some sensors use
`AM 270`; FSK devices need one of the `FM` presets.

## Scanner

A **narrowband RSSI scanner**: the radio measures one frequency at a time and
steps through the scan list, so it shows activity on those frequencies, not a
wideband picture of the whole band. Four rows are visible at a time:

```
 RSSI scan         NF-98 T+10      <- state, noise floor, threshold
* 433.92 [|||||||   |  ] -61  3    <- dot = active now
```

- **Dot** on the left: the frequency is active right now.
- **Bar**: latest RSSI, −110 dBm (empty) to −30 dBm (full). The tall tick is
  the **peak hold**; the small marks above and below the bar are the
  **detection threshold** (noise floor + threshold).
- **Number**: latest RSSI in dBm, then how many separate bursts of activity
  were seen (`99+` above 99).
- **Header**: `RSSI scan` while sweeping, `HOLD` when it stopped on a hit,
  `PAUSED` when paused; `NF` is the estimated noise floor (the median of the
  frequencies' floors) and `T+` the threshold.

Each frequency's noise floor is learned from its quiet readings, so it adapts
to your surroundings. A frequency counts as active when it rises the threshold
above its own floor, and stops being active once it drops 3 dB below that.
Each frequency is listened to for the **Scan dwell** time and the strongest
reading in that window is kept, so short bursts are less likely to be missed;
a longer dwell catches more but makes each pass slower. A blue-green LED blink
marks each new burst.

With **Hold on hit** on, the sweep stops on the first frequency that becomes
active, highlights it and keeps measuring only that frequency, so you can press
**OK** to receive on it straight away.

| Button | Action |
|--------|--------|
| Up / Down | Highlight a frequency |
| OK | Open **Receive & Record** tuned to the highlighted frequency |
| Left | Release a hold; otherwise pause / resume the sweep |
| Hold Left | Clear peak holds and activity counts |
| Right | Save the results to the SD card |
| Back | Return to the main menu |

Results are kept while you visit the receiver and come back; they are cleared
when you return to the main menu. **Right** saves them as a CSV file in
`apps_data/radiogeddon/scans/` (for example `SCAN_20261009_163500.csv`) with one
row per measured frequency: `Freq_Hz,Last_dBm,Peak_dBm,Floor_dBm,Hits`.

Frequencies the radio can't tune are skipped. If every frequency in the scan
list is switched off or unusable, the scanner says so.

The scanner only receives. If the radio can't be started you'll see
`No radio` — `Sub-GHz device not found or not responding.`

## Receive & Record

Listens on the selected frequency, decodes known protocols live, and records
raw timings on request.

The screen shows:

- **Header** — frequency and modulation, e.g. `433.92 AM 650`.
- **RSSI** — a live signal-strength bar and value.
- **Status line** — `Left: record RAW`, or while recording
  `REC <n> smp` (adds `FULL` when the recording buffer is full).
- **`Decoded: <n>`** — how many distinct signals were decoded, and the
  highlighted one as `[<i>] <protocol>` with a `Save` button. Before the first
  decode it shows `Listening...`.

| Button | Action |
|--------|--------|
| Up / Down | Highlight a decoded signal (the newest is highlighted automatically) |
| OK | Save the highlighted decoded signal |
| Left | Start RAW recording; press again to stop and save it |
| Back | Stop the radio and return |

The LED blinks green for each new decode and stays red while recording.

**Saving.** Both save actions open a name screen (`Name signal` or
`Name RAW capture`) pre-filled with a timestamp such as `RG_20261009_143005`.
Confirm to write `<name>.sub` to `/ext/apps_data/radiogeddon/signals/`; you'll
hear a success or error tone, and return to the receiver with your decoded list
intact. A name that already exists is overwritten without warning, so edit the
name if you want to keep the earlier capture.

**Good to know**

- Up to 32 decoded signals are kept per session; identical consecutive repeats
  of the same parcel are listed once. Leaving to the main menu clears the list.
- A RAW recording holds up to 16,384 timing samples (`FULL` after that), or
  fewer if little memory is free when you start it. Stop it soon after the
  transmission ends. If there is not enough free memory to record at all, the
  LED blinks red and recording does not start.
- Leaving the screen while recording discards the unsaved recording.
- Stopping a recording that captured nothing simply returns to listening.

## Frequency Hopper

Like *Receive & Record*, but moves through the **hop list** (by default
**315.00 → 390.00 → 433.92 → 868.35 MHz**), listening for the **Hop dwell**
time on each (200 ms by default). Each frequency learns its own noise floor;
when the signal rises the **Threshold** above it, or a signal is decoded, the
hopper **holds** on that frequency. The hold lasts the **Activity hold** time
after the signal was last seen (2 s by default), so a transmission isn't cut
off by a retune. RSSI is checked every 10 ms and decoding runs the whole time.

The header shows the current frequency; the short line on the RSSI bar is the
current frequency's detection level. The status line shows:

- `Hop 2/4  L:lock R:next` while hopping,
- `HOLD 1.8s` while holding on activity (time left),
- `LOCKED` when you locked the frequency,
- `REC <samples>` while an automatic recording is running.

| Button | Action |
|--------|--------|
| Up / Down | Highlight a decoded signal |
| OK | Save the highlighted decoded signal |
| Left | Lock on the current frequency / unlock (pauses / resumes hopping) |
| Right | Go to the next frequency now (also while locked) |
| Hold OK | Statistics: per-frequency activity and the recent activity list |
| Back | Stop the radio and return |

**Statistics** list, for each frequency, the number of activity periods,
decodes, peak and noise-floor RSSI and total time spent holding on activity,
then the last 16 activity periods (frequency, peak, duration, first decoded
protocol, and whether a RAW capture was saved). They are kept while you visit
Statistics or save a signal, and cleared when you return to the main menu.

**Automatic recording** (Settings → `Hop auto-rec`): when activity starts, the
hopper also records RAW until the hold ends, then saves it as
`HOP_<date>_<time>.sub` in the signals folder (a number is added rather than
overwriting an existing file) with a success tone. Captures under 64 samples
are discarded as noise. Recording starts when activity is *detected*, so the
first few milliseconds of a transmission are not in the file. If there is not
enough free memory to record, the hopper keeps working without recording.

The modulation comes from **Settings**.

## Database

Opens the file browser in `/ext/apps_data/radiogeddon/signals`, listing `.sub`
files. Choose a file to open its action menu (titled with the file name):

| Action | What you get |
|--------|--------------|
| `Signal Info & Analysis` | Summary (protocol, frequency, preset, bits/key or samples) and timing or bit analysis |
| `Unknown Protocol Analysis` | For RAW captures: measured timing, noise and frames, then the likely encoding, bit patterns, a frame-by-frame comparison, a constant-vs-changing field map and a device-ID candidate. Shows `Analyzing...` while it reads the whole file |
| `Crypto Analysis` | Static vs. rolling-code classification and key-byte statistics for decoded protocols |
| `Compare with...` | Pick a second file and see what is the same (`=`) and what differs (`~`); for two RAW captures, a timing-match score and a comparison of their frame patterns |
| `Replay (TX)` | Transmit the recording, where permitted |
| `Delete` | Delete the file (asks for confirmation) |

Reports open in a scrolling text view: **Up / Down** to scroll, **Back** to
return to the action menu. Every result is labelled `[CONFIRMED]`,
`[OBSERVED]`, `[HEURISTIC]` or `[HYPOTHESIS]` — see
[Protocol Analysis](PROTOCOL_ANALYSIS.md) for what each report means.

**Delete** shows `Delete recording?`, the file name and
`This cannot be undone.` Press **Right** (`Delete`) to delete or **Left**
(`Cancel`) / **Back** to keep the file.

You can analyse `.sub` files from other sources (for example the stock app's
`/ext/subghz` folder) by copying them into the signals folder.

## Replay (TX)

Transmits a saved recording **only where you are authorized to do so**.

The `Replay / Transmit` screen shows the protocol and frequency and reminds you
that regional limits are enforced by the firmware, that decoded rolling-code
protocols are refused, and that a RAW capture is sent exactly as recorded. Press
**OK** (`Send`) to transmit.

While sending, the screen shows `Transmitting` / `Sending signal...` and the LED
blinks magenta. When it finishes you'll see `Done` / `Signal sent`, and the app
returns to the action menu after a moment.

If the recording can't be sent, you'll see `Error` with one of:

| Message | Meaning |
|---------|---------|
| `Blocked by region` | Your Flipper's region settings don't allow transmitting on this frequency. |
| `Protected/rolling code` | The protocol is dynamic (rolling code), can't be transmitted by the firmware, or isn't known to it. Such files are never replayed. |
| `Unsupported file` | The file couldn't be read, or its modulation preset isn't recognised. |
| `File not found` | The file no longer exists. |
| `No radio device` | The radio isn't available. |
| `Radio busy` | Another radio operation is still running; go back and try again. |

RAW recordings are replayed exactly as captured. Decoded static protocols are
re-generated by the firmware's encoder and sent as a short burst of repeated
frames, like a single button press.

## About

Shows the version (`Version: 1.0.0-beta.2`), the radio device, the list of
modules, the meaning of the analysis labels, where recordings are stored, and
the project address.

## Tips

- Start with the **Scanner** to find which frequency a device uses, then press
  **OK** on it to decode or record there.
- For an unknown device, record a RAW capture while pressing the button
  **several times**, then run *Unknown Protocol Analysis* to see which bits
  change between presses.
- Keep the transmitter close to the Flipper but not touching it; very strong
  signals can distort timings.
