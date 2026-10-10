# Hardware Verification Checklist

RadioGeddon builds for three firmware families and passes all automated checks,
but **none of the items below has been verified on a physical Flipper Zero
yet**. This is the test plan for doing that. Any subset is useful — please
report results (passes and failures alike) with the
[Hardware test report](https://github.com/mrSlime-man/RADIOGEDDON/issues/new?template=hardware_report.yml)
form, quoting the item IDs.

Only transmit with devices you own or are authorized to test, and within your
region's rules.

## Setup

| ID | Step |
|----|------|
| S1 | Install the `.fap` for your firmware ([Installation](INSTALLATION.md)). |
| S2 | Copy `test/fixtures/*.sub` to `/ext/apps_data/radiogeddon/signals/` (create the folder, or launch the app once first). |
| S3 | Optional but helpful: open a device log — connect USB, run `ufbt cli`, type `log`, keep it running. |
| S4 | Note your firmware family and version (Settings → About) and your device's region. |

## End-to-end workflow

| ID | Step | Pass when |
|----|------|-----------|
| W1 | Launch: Apps → Sub-GHz → RadioGeddon. | Main menu lists the six items; no error in the log. |
| W2 | Start the receiver: Settings `433.92`, `AM 650`; open `Receive & Record`. | Header reads `433.92 AM 650`; RSSI bar reacts to a nearby remote; `Listening...` (not `No radio`). |
| W3 | Capture a supported transmission: a fixed-code remote, or send `princeton_ref_a.sub` from a second Flipper. | `Decoded: 1` with `[1] <protocol>`; green LED blink. |
| W4 | Review decoded info: Up/Down through the list. | Each entry shows its protocol name; newest stays selected. |
| W5 | Save: OK to store the decode; then Left / press / Left to record RAW and save it. | Success tone each time; list kept on return; red LED only while recording. |
| W6 | Reopen: Database → each new file → `Signal Info & Analysis`. | Correct frequency and preset; protocol/bits/key for the decode; `RAW` + sample count + pulse range for the capture. |
| W7 | Analyse: run `Signal Info & Analysis` and `Unknown Protocol Analysis` on the RAW file. | Plausible timing groups and Te; measured timing under `[OBSERVED]`; an encoding with a confidence % and the bit patterns under `[HYPOTHESIS]`. |
| W8 | Compare: `princeton_ref_a` vs `princeton_ref_b`, then two captures of the same button. | `= Proto`, `= Freq`, `~ Key` for the fixtures; for two same-button captures started the same way, a high `RAW timing match` (it is alignment-sensitive, see Protocol Analysis) and *Same frame pattern*. |
| W9 | Replay (only where authorized): open a RAW or static-code file → `Replay (TX)` → `Send`. | `Transmitting` then `Signal sent`; a receiver/second Flipper sees it. A rolling-code file reports `Protected/rolling code`; a region-disallowed frequency reports `Blocked by region`. |
| W10 | Exit: Back out through every screen to the launcher. | No crash or hang; radio LED off; log shows a clean exit. |

## Per-feature checks

| ID | Check | Pass when |
|----|-------|-----------|
| F1 | Scanner sweeps the table; bars update; OK on a frequency opens the receiver tuned to it. | Matches description; Back exits cleanly. |
| F1a | Scanner noise floor and detection: leave it running with no transmitters nearby, then press a 433.92 MHz remote a few times. | Quiet frequencies show no dot and 0 counts; `NF` reads a plausible floor (around −90 to −105 dBm); 433.92 shows the dot while pressed and its count goes up by one per press. |
| F1b | Scanner controls: Left (pause/resume), hold Left (counts and peaks clear), Right (save). | `PAUSED` shown while paused; a `SCAN_*.csv` file appears in `apps_data/radiogeddon/scans/` with one row per frequency. |
| F1c | Hold on hit: enable it in Settings, open the Scanner and press a remote. | Header shows `HOLD`, the active frequency is highlighted, OK opens the receiver on it; back in the Scanner, Left resumes the sweep and earlier counts are still there. |
| F1d | Scan list and dwell: choose `387-464`, then a custom list with two frequencies; try 5 ms and 100 ms dwell. | Only the chosen frequencies are listed; a pass is visibly faster at 5 ms. |
| F1e | Settings persistence: change frequency, modulation and scan options, exit the app and reopen it. | The chosen values are restored. |
| F2 | Frequency Hopper cycles 315 / 390 / 433.92 / 868.35 MHz. | Displayed frequency changes; it holds on a band carrying a transmission long enough to decode; OK saves; Back releases the radio. |
| F2a | Hopper detection: press a remote on one of the hop frequencies. | Status shows `HOLD` with a countdown; it stays while you keep pressing and resumes `Hop n/m` after the Activity hold time; cyan LED blink on activity. |
| F2b | Lock and next: Left, then Right a few times, then Left. | `LOCKED`; Right steps the frequency; Left resumes hopping. |
| F2c | Statistics: hold OK after some activity. | Per-frequency counts and the recent-activity list match what happened; Back returns to the hopper with the decoded list intact. |
| F2d | Auto-record: enable `Hop auto-rec`, press a remote on a hop frequency. | Success tone when the hold ends; a new `HOP_*.sub` in the Database opens and analyses as RAW; two captures in the same second get `_2`. |
| F2e | Repeated use: enter and leave the Hopper ten times with activity, with a device log open. | No crash or hang; the `RadioGeddonHopperScene` free-heap values on exit return to the same level. |
| F3 | Crypto Analysis on a static protocol vs a rolling-code (KeeLoq-family) capture. | Static → `[CONFIRMED] Static code`; rolling → `[CONFIRMED] Dynamic code` and an explicit "no key recovery" note. |
| F4 | Unknown Protocol Analysis across several presses in one RAW capture. | Same fixed remote → 0 changing bits; rolling-code remote → some changing bits with a device-ID candidate; timing under `[OBSERVED]`, structure under `[HYPOTHESIS]`. |
| F4a | Unknown Protocol Analysis on a capture of a fixed-code remote whose code is known (for example one the stock Sub-GHz app decodes as Princeton). | Encoding `PWM`; the pattern's hex matches the stock app's key; several frames grouped as identical repeats. |
| F4b | A long RAW capture (30 s or more, several presses with noise between them). | The analysis finishes without a crash or *Not enough free memory*; the frame list shows noise frames between the presses; note how long it took. |
| F4c | Compare two separate RAW captures of the same button, one started early and one started late. | `RAW timing match` may be low, but the `[HYPOTHESIS] patterns` part says *Same frame pattern*. |
| F4d | Pulse Timeline on a RAW capture with several presses: OK a few times, hold OK, Up/Down, hold Left/Right. | Opens on the first frame; OK steps frame by frame (`Fr n/m` counts up), hold OK goes back; zooming keeps the centre; panning past the loaded part shows `Loading...` briefly and the waveform continues; the end of the recording shows a dotted line. |
| F4e | Pulse Timeline on a long capture (30 s or more), then Back, repeated five times, with a device log open. | No crash; scrolling stays responsive; free heap returns to the same level after leaving. |
| F5 | History: send rapid repeated transmissions. | No crash; identical consecutive parcels listed once; list caps at 32. |
| F6 | RAW capture of a long signal: record for 2 minutes with presses now and then, then save. | REC time and sample count rise steadily past 16,384 samples; no `lost`; the result screen shows the samples and duration; the file opens in Database, analyses and opens in Pulse Timeline. |
| F6a | Stock-app compatibility: open a RadioGeddon RAW capture in the firmware's Sub-GHz app (Saved) and send it. | The stock app lists and plays it like its own RAW files. |
| F6b | Slow card / noisy input: record on AM 650 with no signal (pure noise) for 30 s, ideally on an old or nearly full card. | Note whether `buf` or `lost` appears. If `lost` appears, the saved file opens and its analysis shows `Lost while recording`. No crash or freeze; Left still stops promptly. |
| F6c | Cancel: start recording, press Back, then Back on the name screen. | No file is added to the Database; recording again works. |
| F6d | Card removed while recording. | The recording stops with `SD card write failed`; no crash; after reinserting the card, recording works again. |
| F6e | Hopper auto-record with long activity (hold a remote for 10 s on a hop frequency). | One `HOP_*.sub` with all of it; the hopper resumes afterwards. |
| F7 | Custom-preset replay: a stock-app RAW `.sub` saved with a custom preset. | Replays on the correct modulation, or is cleanly refused (`Unsupported file`) — never sent on the wrong modulation. |
| F8 | Delete: choose Delete on a saved file. | Confirmation shown; Cancel/Back keeps the file; Delete removes it; back in the Database the file is gone and the highlight stays near where it was. |
| F11 | Database list: open Database with a mix of decoded, RAW and stock-app `.sub` files. | `Reading files...` with a rising percentage, then every file newest first with the right type, frequency and date; the counts at the top match the folder. |
| F11a | Sort and filter: press Left through the sort orders; in Options try each `Show` value and a protocol. | Each order and filter lists the expected files; leaving with Back and reopening a file keeps the order and filter until the main menu. |
| F11b | Search: Options → `Search name`, type part of a name in either case; then save an empty text. | Only matching names, `*` in the header; empty text lists all again. |
| F11c | Duplicates: copy a decoded `.sub` and a RAW capture with qFlipper under new names, then `Reload from SD`. | Both pairs show `=` and `=1`; `Show: Duplicates` lists the four files. |
| F11d | Damaged file: copy a text file renamed to `bad.sub` and a `.sub` cut to 20 bytes into the folder. | Both listed as `BAD` with "Not a readable .sub file"; opening one shows only File details (with the start of the first line), Rename and Delete; no crash. |
| F11e | Large folder and memory: 200+ files; open Database, then `Unknown Protocol Analysis` and `Pulse Timeline` on a long RAW capture from it. | Indexing time noted; the analyses run (or report not enough memory) without a crash; free heap returns to baseline after leaving to the main menu. |
| F12 | Rename: rename a file to a new name, then try an existing name, `a/b`, `.x` and the same name in other letter case. | The new name is listed and opens; the others are refused on the keyboard (`Name already used`, `Not allowed`, `No . or space`); no file is replaced. |
| F12a | File details on a decoded file, a RAW capture and a duplicate. | Size and date match qFlipper; type, frequency and preset match Signal Info; the duplicate's copies are named. |
| F12b | Save report to SD on a RAW capture and on a decoded signal; then again on the same file. | `Report saved`, `reports/<name>.txt`, then `<name>_2.txt`; the files on the card contain the labelled sections shown on screen. |
| F12c | Save report with the SD card nearly full or write-protected. | `Report not saved` with a reason; no partial report left on the card. |
| F12d | Menu position: open a file, choose a report lower in the menu, press Back. | The menu returns with the same item highlighted. |
| F13 | Progress: on a RAW capture of 1 MB or more, open it, then run `Unknown Protocol Analysis`, `Pulse Timeline` and `Save report to SD`. | `Opening...` while it opens; each long step shows a percentage that rises steadily while the work runs; the results are complete. Note how long each step takes. |
| F13a | Sort persistence: in the Database press Left to `Name`, go back to the main menu, quit and restart the app, open the Database. | The list opens sorted by `Name`. |
| F13b | Shortcuts and messages: in the Database hold OK on a file; then in a file's menu choose `Compare with...` and pick a `BAD` file. | Hold OK opens File details directly and Back returns to the list; the bad pick shows `Cannot compare` with the error tone and returns to the menu. |
| F14 | External module: connect a CC1101 module (as for the stock app), Settings → `Radio` → `External`. | Stays `External`; the Scanner header reads `RSSI EXT`, Receive shows `EXT`, About says external; a remote is received and decoded through it. |
| F14a | No module: with nothing connected choose `External`. | `No external radio` with the error tone; Settings shows `Internal`; Scanner and Receive work on the internal radio. |
| F14b | 5 V: with a module powered from pin 1, toggle `Ext radio 5V` off, then on. | Off: `No external radio`, back to internal (if the module needs 5 V). Leaving the app turns the 5 V pin off again (check with a meter or the module LED). |
| F14c | Restart with `External` saved, once with the module and once without. | With it: `EXT` shown. Without: internal radio, Settings shows `Internal`, no crash or hang at start. |
| F14d | Replay through the module on an allowed frequency, then on one the region forbids. | Allowed: transmits. Forbidden: `Blocked by region`, nothing sent. |
| F14e | Unplug the module while on the main menu, then open Receive and Replay. | `No radio` / a replay error; no hang. (Unplugging while receiving is not supported by the firmware driver.) |
| F9 | SD card removed mid-session. | Save/open fail gracefully with an error tone; no crash. |
| F10 | Memory: several capture/save/open/replay cycles. | Free heap (CLI `free` or the log) returns to baseline; no growth across cycles. |

## Per-firmware load check

Install each build on its matching firmware; installing the wrong one should
produce an API error rather than a crash (see [Troubleshooting](TROUBLESHOOTING.md#the-app-wont-open)).

| ID | Check | Pass when |
|----|-------|-----------|
| L1 | `radiogeddon-official.fap` on Official firmware. | Launches. |
| L2 | `radiogeddon-unleashed.fap` on Unleashed. | Launches. |
| L3 | `radiogeddon-roguemaster.fap` on RogueMaster. | Launches. |

## Results log

Copy this into a hardware-report issue and fill it in:

| ID | Firmware + version | Pass/Fail | Notes (observed behaviour, log excerpt) |
|----|--------------------|-----------|------------------------------------------|
| W1 | | | |
| W2 | | | |

When results come in, [VERIFICATION.md](VERIFICATION.md) is updated to move
confirmed items into its evidence-backed table.
