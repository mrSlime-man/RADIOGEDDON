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
| `Database` | List, sort, filter and search saved recordings; analyse, compare, replay and delete them |
| `Settings` | Choose the frequency and modulation |
| `About` | Version, modules, label legend and storage location |

## Settings

| Setting | Values | Default |
|---------|--------|---------|
| `Frequency MHz` | 300.00, 303.875, 304.25, 310.00, 315.00, 318.00, 390.00, 418.00, 433.075, 433.42, **433.92**, 434.42, 434.775, 438.90, 464.00, 779.00, 868.35, 915.00, 925.00, or press **OK** to type one (see below) | 433.92 |
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
| `Radio` | **`Internal`**, `External` (a CC1101 module on the GPIO pins; see below) | Internal |
| `Ext radio 5V` | `Off`, **`On`**: power the external module from GPIO pin 1 | On |

Use **Left / Right** to change a value and **Back** to apply it. The
frequency is used by *Receive & Record*; the modulation is used by the
*Scanner*, *Receive & Record* and the *Frequency Hopper* (*Replay* always uses
the recording's own modulation). `Threshold` applies to both the *Scanner* and
the *Frequency Hopper*; the `Scan` settings apply to the Scanner and the `Hop`
settings and `Activity hold` to the Hopper.

### Custom frequency

Press **OK** on `Frequency MHz` to type any frequency in kHz, for example
`433075` for 433.075 MHz, then select the enter key. The keyboard takes 281000
to 962000; the radio in use then decides whether it can tune there:

- Official firmware's radio, and an external CC1101 module, tune 300-348,
  387-464 and 779-928 MHz.
- Unleashed and RogueMaster let the internal radio tune 281-361, 378-481 and
  749-962 MHz.

If it cannot, you see `Cannot tune there`, the frequency is not changed, and
**Back** returns to the keyboard with what you typed. A custom frequency is
shown in Settings as typed; **Left / Right** move from it to the nearest ones
in the list. It is used by *Receive & Record*, like any frequency chosen there;
the Scanner and the Hopper keep to their lists. It is saved with the other
settings, and if the radio in use at the next start cannot tune it, 433.92 MHz
is used instead. Choosing a frequency does not make transmitting on it
allowed: *Replay* is still checked against the firmware's region rules,
whatever frequency a recording was made on.

Settings are saved to `apps_data/radiogeddon/settings.txt` on the SD card when
you leave the Settings screen, and are restored the next time the app starts.
A missing or damaged file just means the defaults are used. The same file also
keeps the Database sort order and the memory a receive session needed
(`Radio_heap`, see [Receive & Record](#receive--record)).

### External CC1101 module

RadioGeddon can use a CC1101 module connected to the GPIO pins, through the
firmware's own external-radio driver, wired the same way the stock Sub-GHz app
expects. Set `Radio` to `External`:

- The app checks that a module answers before using it. If none does, you see
  `No external radio` and the internal radio stays in use.
- With `Ext radio 5V` on, the 5 V pin is switched on while the module is in
  use, as the stock app does, and switched off when you leave the app (unless
  it was already on). Turn it off if your module is powered another way.
- The Scanner header reads `RSSI EXT`, and *Receive & Record* and the
  *Frequency Hopper* show `EXT` at the top right; *About* names the radio.
- The choice is saved. At the next start the external module is used again if
  it answers; otherwise the app quietly uses the internal radio and Settings
  shows `Internal`.
- Transmitting through the module is checked against your Flipper's region
  settings exactly like the internal radio, and the module is checked again
  just before it transmits.

This has not yet been tested with a real module (see the
[hardware checklist](HARDWARE_CHECKLIST.md)).

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
- **Status line** — `Left: record RAW`, or while recording the elapsed time
  and sample count, e.g. `REC 12.3s 4521`. On the right it shows `lost <n>`
  if samples had to be dropped because the SD card fell behind, or
  `buf <n>%` once the write buffer is at least half full.
- **`Decoded: <n>`** — how many distinct signals were decoded, and the
  highlighted one as `[<i>] <protocol>` with a `Save` button. Before the first
  decode it shows `Listening...`.

| Button | Action |
|--------|--------|
| Up / Down | Highlight a decoded signal (the newest is highlighted automatically) |
| OK | Save the highlighted decoded signal. While recording, it stops the recording and asks for its name first; the decoded list is kept |
| Left | Start RAW recording; press again to stop and name it |
| Back | While recording: stop and name it. Otherwise: stop the radio and return |

The LED blinks green for each new decode and stays red while recording.

**Saving.** Both save actions open a name screen (`Name signal` or
`Name RAW capture`) pre-filled with a timestamp such as `RG_20261009_143005`.
Confirm to save `<name>.sub` in `/ext/apps_data/radiogeddon/signals/`. If that
name is taken, `_2`, `_3` and so on is added, so nothing is overwritten. A
result screen shows the final name and, for RAW captures, the sample count,
duration and any lost samples; you then return to the receiver with your
decoded list intact. Press **Back** on the name screen to discard a RAW
capture.

**Good to know**

- Up to 32 decoded signals are kept per session; identical consecutive repeats
  of the same parcel are listed once. Leaving to the main menu clears the list.
- A RAW recording is written to the SD card as it runs, so it can be as long
  as the card allows. Stopping takes a moment while the last samples are
  written (`Writing to SD...`).
- If the card cannot keep up (very noisy input on a slow card), some samples
  are dropped: the line shows `lost <n>` and the saved file notes it. The
  timing jumps where samples were lost, so record again if that matters.
- If recording cannot start, the LED blinks red and the hint line says why
  (`REC: Not enough memory` or `REC: Cannot create file`). If the card fails
  while recording, it stops and shows `SD card write failed`.
- Stopping a recording that captured nothing shows `Nothing captured` and
  returns to listening.
- Before the radio starts, the app checks that the memory a receive session
  needed last time (on this firmware) is still free, plus a 6 KB margin. If it
  is not, the screen shows `Not enough memory` with the figures, for example
  `Radio needs ~27 KB, 15 KB free.`, and the radio is not started (see
  [Troubleshooting](TROUBLESHOOTING.md#not-enough-memory-when-opening-receive-hopper-or-decode-with-firmware)).
  The Frequency Hopper does the same.

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
- the REC line (time, samples, `lost` or `buf`) while an automatic recording
  is running.

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
first few milliseconds of a transmission are not in the file. The capture is
streamed to the SD card, so long activity is kept whole. If there is not
enough free memory to record, the hopper keeps working without recording; if
the card fails, you hear the error tone and the capture is not kept.

The modulation comes from **Settings**.

## Database

Lists the `.sub` files in `/ext/apps_data/radiogeddon/signals`, newest first
(or in the sort order you used last time). It shows `Reading files...` with a
percentage while it reads the start of each file (and, for RAW captures of
exactly the same size, the whole file to check for copies).

```
Signals              Date 12/14
RG_20261009_141205          RAW
garage_left            =Princet
porch_remote            Princet
old_capture                 BAD
433.92  10-09 14:12
```

- The top line shows what is listed (`Signals` for everything, a filter or a
  protocol, after a `*` when a name search is active), the sort order, and how many
  files are shown out of how many are in the folder.
- Each row shows the name (without `.sub`) and its type: the protocol (first
  7 letters) for a decoded signal, `RAW` for a capture, `?` for a Sub-GHz
  file without a protocol and `BAD` for a file that is not a readable `.sub`. A leading `=` marks a
  duplicate: a decoded signal with the same protocol, frequency, bit count and
  key as another file, or a RAW capture whose contents are identical to
  another one.
- The bottom line shows the highlighted file's frequency in MHz and its date,
  and `=N` when it has N duplicates.

| Key | Action |
|-----|--------|
| **Up / Down** | Move (hold to scroll; wraps around) |
| **OK** | Open the file's action menu |
| **Hold OK** | Show the file's details straight away |
| **Left** | Next sort order: Date (newest first), Name, Freq, Protocol |
| **Right** | Options |
| **Back** | Main menu |

**Options** (Right):

| Option | Values |
|--------|--------|
| `Sort by` | `Date`, `Name`, `Freq` (lowest first; files without one last), `Protocol` (damaged files last) |
| `Show` | `All`, `RAW`, `Decoded`, `Duplicates`, `Damaged` (unreadable or other `.sub` types), then one value per protocol found |
| `Search name` | OK opens the keyboard; lists only names containing the text, ignoring case. Save an empty text to clear it |
| `Reload from SD` | OK re-reads the folder (after copying files in with qFlipper, for example) |
| `Files indexed` | How many files are listed. When memory is short only part of a very large folder is indexed, and this shows `N of M` |

The filter and search last until you return to the main menu; the sort order
is saved with the settings and used the next time. Opening a large RAW
capture shows `Opening...` while the app counts its samples. After a
rename or delete the list is re-read and keeps your place. Choose a file to
open its action menu (titled with the file name; it opens where you left it
when you come back from a report):

| Action | What you get |
|--------|--------------|
| `Signal Info & Analysis` | Summary (protocol, frequency, preset, bits/key or samples) and timing or bit analysis |
| `Decode with Firmware` | RAW captures only: runs the firmware's decoders, the ones *Receive* uses, over the whole recording and lists each signal they decode once, `[CONFIRMED]`, with how many times and when it was decoded and the decoder's description. Shows `Decoding...` and a percentage meanwhile. The radio is not used. If the decoders don't fit in memory, `Not enough memory` (`Decoders need ~N KB`) appears instead, as for *Receive* |
| `Unknown Protocol Analysis` | For RAW captures: measured timing, noise and frames, then the likely encoding, bit patterns, a frame-by-frame comparison, a constant-vs-changing field map and a device-ID candidate. Shows `Analyzing...` and a percentage while it reads the whole file |
| `Pulse Timeline` | RAW captures only: the recording as a zoomable waveform (see below) |
| `Crypto Analysis` | Static vs. rolling-code classification and key-byte statistics for decoded protocols |
| `Compare with...` | Pick a second file and see what is the same (`=`) and what differs (`~`); for two RAW captures, a timing-match score and a comparison of their frame patterns (`Comparing...` with a percentage meanwhile). Picking a file that can't be read shows `Cannot compare` |
| `Replay (TX)` | Transmit the recording, where permitted |
| `File details` | File name, size, date modified, type, frequency, preset, sample count or bits, and the names of its duplicates |
| `Rename` | Type a new name (`.sub` is added). Names the SD card can't store (`< > : " / \ | ? *`, or a `.` or space at the start or end) and names already used are refused, so renaming never replaces another file. The SD card ignores letter case, so a name that differs only in case counts as used |
| `Save report to SD` | Writes *Signal Info & Analysis*, plus *Decode with Firmware* and *Unknown Protocol Analysis* for a RAW capture or *Crypto Analysis* for a decoded signal, to `apps_data/radiogeddon/reports/<name>.txt`. Shows `Writing report...` (with a percentage while a RAW capture is analysed), then the file name. An existing report is never replaced: the new one gets `_2`, `_3` and so on |
| `Delete` | Delete the file (asks for confirmation) |

A **damaged file** (`BAD` in the list) opens with only `File details` (which
shows the start of its first line), `Rename` and `Delete`.

Reports open in a scrolling text view: **Up / Down** to scroll, **Back** to
return to the action menu. Every result is labelled `[CONFIRMED]`,
`[OBSERVED]`, `[HEURISTIC]` or `[HYPOTHESIS]` — see
[Protocol Analysis](PROTOCOL_ANALYSIS.md) for what each report means.

### Pulse Timeline

Shows a RAW capture as a waveform: carrier-on pulses on the upper line,
carrier-off gaps on the lower one. It shows `Indexing...` and a percentage while it reads the
file once, then opens on the first decodable frame.

| Button | Action |
|--------|--------|
| **Left / Right** | Pan a quarter screen (hold to keep moving) |
| **Up / Down** | Zoom in / out, from 5 µs to 5 ms per pixel, keeping the centre |
| **OK** | Jump to the next frame |
| **Hold OK** | Jump to the previous frame |
| **Back** | Return to the action menu |

The top line shows the time at the left edge and the zoom (`50us/px`). Small
triangles mark where frames start; pulses wide enough show their duration in
µs. The bar under the waveform shows where the screen sits in the whole
recording, and `Fr 3/12` / `#1240/9120` give the current frame and the sample
index at the left edge. Only about a thousand samples are held at a time;
moving beyond them shows `Loading...` for a moment. A dotted line marks the
end of the recording.

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
| `Bad custom preset` | The file's own CC1101 register list is damaged, or writes something other than a setting (a radio command). Nothing was sent to the radio. |
| `File not found` | The file no longer exists. |
| `No radio device` | The radio isn't available. |
| `Radio busy` | Another radio operation is still running; go back and try again. |

RAW recordings are replayed exactly as captured. Decoded static protocols are
re-generated by the firmware's encoder and sent as a short burst of repeated
frames, like a single button press.

## About

Shows the version (`Version: 1.0.0-beta.4`), the radio device, memory
figures, the list of modules, the meaning of the analysis labels, where
recordings are stored, and the project address.

The memory figures are read from the firmware's heap counters when About
opens:

| Line | Meaning |
|------|---------|
| `Free now` | Free heap at this moment. |
| `Largest block` | The biggest single allocation that could succeed now. |
| `At app start` | Free heap when RadioGeddon started. |
| `Lowest in app` | The lowest free heap the app has seen in this run. It samples on every screen tick (10 times a second) and right after its large allocations; `(after: …)` names the last step it had started, such as `Receiver`, `Recording` or `Reading files...`. |
| `Peak app use` | `At app start` minus `Lowest in app`. |
| `Low since boot` | The firmware's lowest free heap since the Flipper started, for any app. |
| `Radio session` | What starting Receive or the Hopper took (decoders, keystore, worker), as measured this run or kept from an earlier run on the same firmware; `not measured yet` until one has run. |
| `Total heap` | The heap's size. |

When the app closes it writes the same summary to the log
(`RadioGeddonMem`), which helps when reporting a memory problem.

## Tips

- Start with the **Scanner** to find which frequency a device uses, then press
  **OK** on it to decode or record there.
- For an unknown device, record a RAW capture while pressing the button
  **several times**, then run *Unknown Protocol Analysis* to see which bits
  change between presses.
- Keep the transmitter close to the Flipper but not touching it; very strong
  signals can distort timings.
