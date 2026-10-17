# Hardware Verification Checklist

RadioGeddon builds as two editions — **Catalog** for Official firmware and
**Full** for RogueMaster, Momentum and Unleashed — and passes all automated
checks, but **none of the items below has been verified on a physical Flipper
Zero yet** (beyond one beta 2 launch report on RogueMaster). This is the test plan for doing that. Any subset is useful — please
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
| S5 | Note the edition and version RadioGeddon's About screen shows (`RadioGeddon Catalog` or `RadioGeddon Full`, `1.0.0-beta.7`). |

## End-to-end workflow

| ID | Step | Pass when |
|----|------|-----------|
| W1 | Launch: Apps → Sub-GHz → RadioGeddon (Catalog) or RadioGeddon Full. | Catalog: the menu lists Scanner, Receive & Record, Frequency Hopper, Database, Settings, About. Full: header `RadioGeddon Full` and Scan, Receive & Record, Analyze, Database, Sessions, Settings, About (Scan holds Frequency Scanner, Adaptive Scanner, RF Activity Logger, Range Scanner, Waterfall, Frequency Hopper, Favorites; in the Dolphin Lab style these are on the dashboard). No error in the log. |
| W2 | Start the receiver: Settings `433.92`, `AM 650`; open `Receive & Record`. | Header reads `433.92 AM 650`; RSSI bar reacts to a nearby remote; `Listening...` (not `No radio`). |
| W3 | Capture a supported transmission: a fixed-code remote, or send `princeton_ref_a.sub` from a second Flipper. | `Decoded: 1` with `[1] <protocol>`; green LED blink. |
| W4 | Review decoded info: Up/Down through the list. | Each entry shows its protocol name; newest stays selected. |
| W5 | Save: OK to store the decode; then Left / press / Left to record RAW and save it. | Success tone each time; list kept on return; red LED only while recording. |
| W6 | Reopen: Database → each new file → `Signal Info & Analysis`. | Correct frequency and preset; protocol/bits/key for the decode; `RAW` + sample count + pulse range for the capture. |
| W7 | Analyse: run `Signal Info & Analysis` and `Unknown Protocol Analysis` on the RAW file. | Plausible timing groups and Te; measured timing under `[OBSERVED]`; an encoding with a confidence % and the bit patterns under `[HYPOTHESIS]`. |
| W8 | Compare: `princeton_ref_a` vs `princeton_ref_b`, then two captures of the same button. | `= Proto`, `= Freq`, `~ Key` for the fixtures; for two same-button captures started the same way, a high `RAW timing match` (it is alignment-sensitive, see Protocol Analysis) and *Same frame pattern*. |
| W9 | Replay (only where authorized): open a RAW or static-code file → `Replay (TX)` → `Send`. | `Transmitting` then `Signal sent`; a receiver/second Flipper sees it. A rolling-code file reports `Protected/rolling code`; Catalog edition: a frequency the firmware's region forbids shows `TX NOT ALLOWED` on the Replay screen and `TX refused` with the region and frequency on Send (see C1); Full edition: the firmware's own refusal shows `Firmware blocked TX`. |
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
| F1f | Custom frequency: Settings → OK on `Frequency MHz`, type `433075`, enter; open Receive (and, if you have one, press a remote that sends on a frequency outside the list); then type `370000` (and on Official, `350000`); restart the app. | Settings shows `433.075` and Receive `433.075 AM 650`; a remote on a typed frequency decodes as it would on a listed one; `370000`, and `350000` on Official, give `Cannot tune there` and Back shows the typed value; after the restart `433.075` is still set. A RAW capture made there and replayed is still subject to the region check (W9). |
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
| F4f | Comma-separated RAW: copy the firmware's `hormann_hsm_raw.sub` (from `applications/debug/unit_tests/resources/unit_tests/subghz/` in the firmware source) to the signals folder; open it with `Signal Info & Analysis`, `Unknown Protocol Analysis` and `Pulse Timeline`. | Signal Info and the analysis report 2560 samples (the host format test reads the same), not 0 or one per line; the analysis finds a frame and the timeline shows a waveform. |
| F4g | Decode with Firmware: record a RAW capture of a fixed-code remote the stock Sub-GHz app decodes (for example Princeton), pressing it a few times; open it from the Database with `Decode with Firmware`. Repeat on a capture of noise only and on the firmware's `princeton_raw.sub` test file (copied as in F4f). | `Decoding...` with a rising percentage; the remote is listed once as `[CONFIRMED]` with the protocol and key the stock app's Read shows, a count that grows with the presses and plausible times; noise only shows *No protocol decoded*; the test file decodes as Princeton. No crash, and free heap (About) returns to the same level afterwards. Note how long a 30 s capture takes. |
| F5 | History: send rapid repeated transmissions. | No crash; identical consecutive parcels listed once; list caps at 32. |
| F5a | Official firmware, if you have a CAME Atomo or Alutech AT-4N remote: press it while Receive is running. | It is listed by name with its bit count and key and the note that serial, button and counter need a rainbow table; no crash (beta 3 and earlier could crash here). |
| F6 | RAW capture of a long signal: record for 2 minutes with presses now and then, then save. | REC time and sample count rise steadily past 16,384 samples; no `lost`; the result screen shows the samples and duration; the file opens in Database, analyses and opens in Pulse Timeline. |
| F6a | Stock-app compatibility: open a RadioGeddon RAW capture in the firmware's Sub-GHz app (Saved) and send it. | The stock app lists and plays it like its own RAW files. |
| F6b | Slow card / noisy input: record on AM 650 with no signal (pure noise) for 30 s, ideally on an old or nearly full card. | Note whether `buf` or `lost` appears. If `lost` appears, the saved file opens and its analysis shows `Lost while recording`. No crash or freeze; Left still stops promptly. |
| F6c | Cancel: start recording, press Back, then Back on the name screen. | No file is added to the Database; recording again works. |
| F6d | Card removed while recording. | The recording stops with `SD card write failed`; no crash; after reinserting the card, recording works again. |
| F6e | Hopper auto-record with long activity (hold a remote for 10 s on a hop frequency). | One `HOP_*.sub` with all of it; the hopper resumes afterwards. |
| F6f | OK while recording: start recording in Receive, wait for a decode, press OK. | The RAW name screen appears first; after saving, the receiver shows the decoded list again and OK saves the decode. Both files are in the Database. |
| F6g | Saved keys in the stock app: save one decode from Receive and one from the Hopper (OK on a decode), on `AM 650`; open each in the firmware's Sub-GHz app (Saved), then in RadioGeddon (Database → the file). | The stock app opens both and shows the protocol and key (not `Cannot parse file`). RadioGeddon's `Signal Info & Analysis` shows `Preset: FuriHalSubGhzPresetOok650Async`. For a static code, Replay offers `Send` (send only where authorized). If you kept a key saved by beta 4 or earlier, it shows `Bad custom preset` in Replay until fixed as [Troubleshooting](TROUBLESHOOTING.md#a-saved-key-wont-open) says, and then opens in both. |
| F7 | Custom-preset replay: a stock-app RAW `.sub` saved with a custom preset; then a copy with the last four bytes of `Custom_preset_data` removed (open that copy in RadioGeddon only; the stock app does not check it). | The original replays on the correct modulation, or is cleanly refused (`Unsupported file`) — never sent on the wrong modulation. The cut copy shows `Bad custom preset` and nothing is sent. |
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
| F14d | Replay through the module on an allowed frequency, then on one the region forbids. | Allowed: transmits. Forbidden: Catalog `TX refused` with the region; Full: whatever the firmware's external driver decides (`Firmware blocked TX` if it refuses). Nothing is sent when refused. |
| F14e | Unplug the module while on the main menu, then open Receive and Replay. | `No radio` / a replay error; no hang. (Unplugging while receiving is not supported by the firmware driver.) |
| F15 | Memory figures: open About right after launch, then open Receive for a few seconds, go back and open About again. | First visit: `Radio session: not measured yet` (on a firmware never measured before). Second: a non-zero `Radio session` (note the value and the firmware), `Lowest in app` below `At app start` with `(after: Receiver)`, `Free now` back near its first value. |
| F15a | Session cost kept: quit and restart the app, open About. | `Radio session` shows the same value as before the restart. `settings.txt` holds `Radio_heap` and `Radio_heap_fw`. |
| F15b | Refusal: with a `Radio_heap` measured, edit `settings.txt` to set `Radio_heap` larger than the free heap (for example `200000`), restart and open Receive and the Hopper. | `Not enough memory` with the figures and the error tone; Back returns to the menu; no crash. Restore the value (or delete the two lines) and Receive starts again. |
| F15c | Repeated sessions: enter and leave Receive, the Hopper and the Database ten times each (record once in Receive), with a device log open, then open About. | No crash; the `RadioGeddonMem` summary on exit and `Free now` return close to `At app start`; `Lowest in app` stays above 0 with a sensible `(after: …)` label. |
| F9 | SD card removed mid-session. | Save/open fail gracefully with an error tone; no crash. |
| F10 | Memory: several capture/save/open/replay cycles. | Free heap (CLI `free` or the log) returns to baseline; no growth across cycles. |

## Editions (beta 6)

| ID | Check | Pass when |
|----|-------|-----------|
| E1 | About on each edition. | `RadioGeddon Catalog` / `RadioGeddon Full`, `Edition:` line, version `1.0.0-beta.7`, `Region:` with your device's region (or `--`). |
| E2 | Shared data: with recordings, favorites and a profile made in the Full edition, install the Catalog edition (on a device where both can run, or by moving the SD card), open its Database and Settings, change a setting, then go back to the Full edition. | The Catalog edition lists the same recordings; nothing is deleted or renamed; back in Full, favorites, profiles and the range settings are unchanged. |
| E3 | Updating from beta 5: on RogueMaster or Unleashed, install the Full edition next to the old beta 5 file, then delete the old file. | Both appear in the Apps menu until the old one is deleted; the Full edition opens the beta 5 recordings and settings. |

## Range Scanner, favorites and profiles (Full edition)

| ID | Check | Pass when |
|----|-------|-----------|
| R1 | Settings → Radio bands on the internal radio. | RogueMaster / Momentum / Unleashed: 281.00–361.00, 378.00–481.00, 749.00–962.00 MHz; the firmware region's TX bands below. With an external module selected, the module driver's bands. |
| R2 | Range Scanner: Start 433000, End 435000, Step 25 kHz, Dwell 5 ms. Start scan; press a 433.92 MHz remote. | `Points/sweep` reads `81 0.7s` (about); the screen shows `CALIBRATING` briefly, then `RANGE`; a bar rises at 433.92 while pressed, a dot keeps the peak, the counter at the cursor rises; Down moves the cursor to the peak. Note the real sweep time shown bottom right. |
| R3 | Across a gap: Start 300000, End 928000, Step 5 MHz. | Accepted with a point count below 256; the sweep never shows an error; the cursor at the band edges reads frequencies inside the bands (no 370 or 600 MHz point). |
| R4 | Limits: Step 1 kHz over 300–928 MHz; then End below Start. | `Points/sweep` shows the count with `>256!` and Start scan says `Too many points`; End below Start moves Start with it. |
| R5 | Pause on hit and receive: turn `Pause on hit` on, press a remote. | Header `HOLD`, cursor on the active point, OK opens Receive there; long OK opens Receive with recording already running. Back returns to the scan (restarted, cursor kept). |
| R6 | Recalibrate, reset, save: hold Up, hold Down, hold Right. | Hold Up: `CALIBRATING` again; hold Down: peaks and counters clear; hold Right: `Results saved` and a `SCAN_*.csv` with one row per measured point. |
| R7 | Profiles: Save profile (accept the suggested name), change the range, Load profile; save again under the same name; Delete profile. | Loading restores every field; saving under an existing name asks `Replace profile?`; Delete asks first; the files are in `apps_data/radiogeddon/profiles/`. |
| R8 | Favorites: add the current frequency, type one (`433075`), try a duplicate and an untunable `100000`; then Receive + record on one; delete one. | Listed sorted; the duplicate says `Already saved`; `100000` is refused by the keyboard; Receive + record starts recording at once; Delete asks first. |
| R9 | Favorites as sources: Settings → Scan source `Favorites`, Hop source `Favorites`; open Scanner and Hopper. | Both use exactly the favorites; with no favorites the Scanner shows `No frequencies to scan` and the Hopper `No frequencies`. |
| R10 | Fine step: Settings → Freq step `1 MHz`, then Left/Right on `Frequency MHz` past 361 MHz; then `List`. | Steps by 1 MHz and jumps 361 → 378 MHz (CFW) without stopping in the gap; back on `List`, Left/Right steps through the frequency list again. |
| R11 | Checksum hypotheses: record several different button presses of a fixed-code remote into one RAW capture; run Unknown Protocol Analysis. | A `Checksum structure` section: either a fit with `fits n/n` or `No common checksum`, or a note that more different frames are needed. Compare with what is known about the remote. |

## Research tools (Full edition, new in beta 7)

These run as modules loaded from the `.fap`; a failure to load reads
`Tool missing` or `Tool not loaded` (please report it with the firmware).

| ID | Check | Pass when |
|----|-------|-----------|
| B1 | Scan → Waterfall (Start 433000, End 435000, Step 25 kHz); press a 433.92 MHz remote a few times. | Header `RSSI sweep` (state `CAL`, then `LIVE`); rows appear on top and move down; the bursts show as dense or solid marks near 433.92 MHz; columns not measured in a sweep show the dotted pattern, not a level. Note the sweep time in the footer. |
| B2 | Waterfall keys: Left/Right, Up/Down, OK; hold OK and try each menu item. | The cursor moves (faster when held); Up/Down scroll with `-N` in the header; OK pauses and resumes; *Receive here* opens Receive on the strongest point under the cursor; *Save history CSV* writes `scans/WF_*.csv` with one row per sweep and empty cells where nothing was measured. |
| B3 | Enter and leave the Waterfall ten times with a 256-point range, then open About. | No crash, no `Not enough memory` on a fresh start; `Free now` returns close to `At app start`. |
| B4 | Bitstream Explorer on a RAW capture of a fixed-code remote (Database → file → Bitstream Explorer); cycle the views with OK. | FRAMES lists frames with matching letters for repeats; BITS, HEX (hold OK shifts the offset 0–7), DIFF (`.` for a fixed-code repeat, `X` where frames differ) and FIELD (Up/Down/Left/Right move the range) agree with each other. |
| B5 | Record 4–8 presses of different buttons of one remote as separate RAW files; Compare several… on one, add the others, Compare now. | A report with `[OBSERVED]` and `[HYPOTHESIS]` lines; the button bits classified as changing (button-like) and the rest constant; nothing called a verified serial. |
| B6 | Sessions → New session, Collect new captures, record twice in Receive; open the session's Recordings; Export report. | Both new recordings are listed; opening one shows its usual menu; the report is in `reports/`; the `.sub` files are unchanged and open in the stock Sub-GHz app. |
| B7 | Sessions → Suggest groups after recording several captures of one remote within a few minutes. | Groups are offered, not created; a group becomes a session only after confirming. |
| B8 | Pull the SD card's power (or reboot) while a session is being saved (add a recording, then reset at once); open Sessions after restarting. | The session opens with either the old or the new list, never damaged; no `.tmp` left in use. |
| B9 | Unknown Protocol Analysis on a RAW capture of a PWM remote (Princeton, CAME, Nice). | The `[HYPOTHESIS] structure` section names PWM; the `Fits:` line gives every reading's score; an `[OBSERVED] A repeats every … ms` line gives how often the pattern repeats. |

## Dolphin Lab (Full edition, new in beta 8)

None of these is verified yet. Record the firmware, and for timing items how
you measured (a phone's slow-motion video of the screen works).

| ID | Check | Pass when |
|----|-------|-----------|
| D1 | Launch the Full edition after a reboot. | The dashboard: the dolphin at its desk on the left, the menu on the right, the status bar with the frequency, preset and `Full`; a short wipe on first entry only. Note how the dolphin looks on the real screen (legible? recognisable?). |
| D2 | Leave the dashboard idle for 10 s, then 50 s, then 2.5 min without pressing anything. | Blinks, breathing, a tail flick, looking around now and then (calm, not flickering); after about 45 s sleepy eyes; after 2 min asleep with `z`s. Any key wakes it at once and is handled (the menu moves). |
| D3 | Up/Down through the whole menu, Left/Right, OK on every item, Back from each, long OK on Scan and on Receive. | Wraps at both ends; Left/Right jump a page; every item opens its screen (`Dolphin Lab` the room); long OK on Scan opens the Frequency Scanner, on Receive starts receiving and recording at once. No key is lost or delayed by the animation. |
| D4 | Settings → UI style → Cyber Lab, Back; then Classic, Back; then Dolphin Lab, Back. Restart the app after each. | The new style shows at once and after a restart (it is saved); Classic is the beta 7 menu with no dolphin or animation; radio behaviour (frequency, preset, scans) is unchanged by any style. |
| D5 | Scanner with Hold on hit off; press a 433.92 MHz remote. | The header badge shows the dolphin's head; a `!` on each detection; the dashboard afterwards shows no leftover scanning. The thick mark under the header moves with the sweep and stops when paused (Left). |
| D6 | Receive & Record: listen, then Left to record, then Left to stop and save. | The badge listens (waves) while receiving; the lamp blinks only while `REC` shows in the receiver; on save the `Saved` card shows the dolphin cheering with the file name and samples, and returns by itself after 2.5 s (or on OK). Repeat with the SD card write-protected: `Save failed`, worried dolphin, the message in full. |
| D7 | A recording that cannot start (fill the card, or force low memory as in F15b) and Back. | The receiver's note or `Not enough memory` with the figures, the dolphin worried; never a REC lamp. |
| D8 | Database → a long RAW capture → Unknown Protocol Analysis, and Decode with Firmware. | A progress card with the dolphin at its terminal and a rising percentage; then a short reveal of the report's first lines and the full report; any key during the reveal opens it at once. |
| D9 | Animation rate: film the dashboard during a scan (Scanner open, Back to it) and in the room's radar station. | Note the frame rate you see (target about 8 per second while active, about 2 idle) and whether anything flickers. |
| D10 | Dolphin Lab room: walk all stations with Left/Right; OK on Oscilloscope and pick a RAW recording; OK on Terminal; Up/Down in the Gallery. | Short wipes between stations; the oscilloscope shows the recording's first pulses with the time span; the terminal's counts match the SD card (recordings in `signals`, `.txt` files in `reports` and `sessions`); every pose in the gallery. The radio is not used (no LED, no RSSI activity). |
| D11 | From the room: OK on Radar (Scanner), Back; OK on Antenna (Receive), Back; repeat 10 times; open About. | The real tools open and work; no crash; `Free now` returns close to `At app start` (the room's module is unloaded before each tool). |
| D12 | Memory: note About's figures on a fresh start in Dolphin Lab and in Classic (restart between). | Note both; the difference should be small (the lab view's few hundred bytes). Compare `At app start` with beta 7 if you have it (about 15 KB less is expected: the larger resident code). |
| D13 | Settings → UI feedback → Minimal, then Full; record, run an analysis, browse the menu, scan with a busy remote. | Minimal: a short vibration when a recording starts and when an analysis finishes. Full: also short sounds, a menu click and at most one blip every 2 s on detections. Off: as beta 7. |
| D14 | Leave the dashboard open for 30 minutes on battery, then the same in Classic. | Note the battery drop of each; the dashboard should not drain noticeably more (it redraws only on changes). |

## Research Lab (Full edition, new in beta 9)

None of these is verified yet. Nothing here transmits; check that the LED
never shows a transmission. Note the firmware, the number of recordings on
the card, and for battery items the start and end charge.

| ID | Check | Pass when |
|----|-------|-----------|
| RL1 | With 20 or more RAW recordings on the card (several of one remote, others of different devices): Analyze → Find Similar... on one recording of the remote. | `Fingerprinting...` with a percentage the first time (note how long it takes); a list of matches where the same remote's other recordings come first with the highest scores; a line with the number compared. |
| RL2 | Run the same search again; then record one more capture and search again. | The second search is quick (only the new file is fingerprinted); the new recording appears in the list if it matches. `fingerprints.idx` exists in `apps_data/radiogeddon`. |
| RL3 | On a match: `Why similar?`, `Open recording`, `Compare with this`, then `Compare all matches`. | The explanation lists agreeing and contradicting features in words; Open shows the recording's menu; Compare shows the two-file report; Compare all opens a multi-capture report. |
| RL4 | Find Similar on a recording with no structure (record a few seconds of noise). | `No structure to match` or matches scored 30 or below — never a confident match. |
| RL5 | Corrupt the index: copy a random file over `fingerprints.idx` with qFlipper, then search. | The search works (the index is rebuilt); no crash. |
| RL6 | Pulse Studio on a RAW capture of a fixed-code remote: cycle every page with OK, Up/Down through frames, hold Right/Left on Wave, hold OK, move the cursor. | Frames change (`Reading frame...` at most briefly); the bits under the symbols match the Bits page; zoom changes the time scale; the marker `A` and the distance in the footer appear; Hex and Diff agree with the Bitstream Explorer on the same file. |
| RL7 | Pulse Studio on a very long RAW recording (1 minute or more) and on a decoded (non-RAW) file. | The long one opens and moves between frames without `Not enough memory`; the decoded one says Pulse Studio needs a RAW capture. Note About's `Free now` before and after. |
| RL8 | Protocol Discovery on captures of a PWM remote (Princeton / CAME / Nice) and a Manchester one if you have it. | A `Readings [HYPOTHESIS]` section listing more than one reading with scores and evidence; the first is the right encoding. Compare the frame length with what the stock Sub-GHz app decodes. |
| RL9 | Scan → Adaptive Scanner → `Balanced`, `Start scanner`; press a 433.92 MHz remote repeatedly for a minute, then stop. | The header shows `Balanced` and `gap x.xs`; 433.92 MHz's share rises while it is busy; every other frequency is still measured (the cursor visits them) and the gap stays a few seconds at most. After you stop, its share falls back over time. |
| RL10 | Adaptive Scanner in `Focused` and `Custom` (with two favorites in the scan list); `Reset learning`. | Focused favours the busy frequency more; Custom favours the favorites; Reset returns every share to even. `Standard` behaves exactly like beta 8's Scanner. |
| RL11 | Scan → RF Activity Logger: 15 min, `Log every 10 s`, part `64 KB`; press a remote now and then; let it finish. | The running screen counts events, rows and KB; at the end a summary with the busiest frequency and coverage; `activity/activity_*.csv` with a header, one row per frequency per 10 s and an `# End` line; a `_summary.txt`. |
| RL12 | Logger with `Until stop` for 8 hours (or overnight) on battery, screen off. | Note the battery drop and the file size; the log has rows for the whole period; no crash; Back stops it and writes the summary. |
| RL13 | Logger with part size `64 KB` on a busy band until the parts fill (or a nearly full SD card). | It stops with a message that the log is full or the card cannot be written; existing files are never overwritten; the CSV parts are complete lines. |
| RL14 | Pull the SD card (or reboot) during a log; open the CSV on a computer. | Every complete line is readable; the summary of a later run is unaffected; the file lacks `# End` (interrupted). |
| RL15 | Research Journal: save a recording, analyse it, run Find Similar and Pulse Studio, export a session report; open Analyze → Research Journal. | Each event is counted once; recent events have the right times; achievements earned show (`New badge!` in the status bar until viewed). Reboot: the journal is kept. |
| RL16 | Settings → Research XP `Off`; repeat an event; open the journal, the room's terminal and Customize. | No XP, level, badge notice or celebration; the journal still counts; every wall decoration is selectable. |
| RL17 | Assistant: after saving a RAW capture, return to the dashboard. Wait; dismiss with long OK; record (Receive → Left). | A tip such as `Analyze it?` as the first menu row; OK opens that tool; long OK removes it for this recording; no tip ever appears while recording; a new tip takes at least 15 s. Settings → Assistant tips Off: none. |
| RL18 | Room 2.0: every station with Left/Right; OK on Workbench, Radar, Terminal, each Rack tool, Corner; customize the dolphin (every row). | Each opens the right screen; the look changes at once on the dashboard and room and survives a restart; nothing uses the radio unless you start a tool. |
| RL19 | Settings → Animation: `Off`, `Minimal`, `Normal`, `Full`; watch the dashboard 30 s each. | Off: a still dolphin; Minimal: only blinks and state changes; Full: more movement. Keys respond equally fast in every level. Optional: 30 min battery drop in Off vs Full. |
| RL20 | Memory: About after a fresh start; then open and close Find Similar, Pulse Studio, Activity Logger, the journal and the room five times each; About again. | `At app start` is close to beta 8's; `Free now` returns close to `At app start` (modules unloaded); no crash. |

## Reliability, recovery and efficiency (unreleased)

| ID | Check | Pass when |
|----|-------|-----------|
| U1 | Start a RAW recording on any signal, wait 10 s, then hold Back+Left to reboot the Flipper (or pull the battery). Start the app again. | `Unsaved recording` asks Keep / Delete with a sample count; Keep saves `Recovered_<time>.sub`, it opens, Decode with Firmware / analysis read it, and at most about 2 s before the reboot is missing. |
| U2 | Repeat U1 and answer Delete; repeat and answer Back. | Delete removes it; Back keeps it (saved as in U1). The next start asks nothing. |
| U3 | Change a setting, and reboot while Settings is being left (several tries, as close to the save as possible). Start the app. | Settings are either the new or the old values, never the defaults. Same for favorites (Full) and a scan profile. |
| U4 | Full edition: Find Similar on a card with 600+ recordings (copies are fine), then again. | The first search indexes them all (no `first 500`); the second recomputes nothing and is fast. Time both and note them. |
| U5 | Full edition: Find Similar, then the Scope row (1 hour, 1 day). | Only recordings saved within the window are ranked; `out of scope` counts the others. |
| U6 | Database with 300+ files: change the sort four times and type a search. | Each change is visibly quick (note any delay over half a second). |
| U7 | Full edition: Scan → Activity Heatmap on a log of an hour or more with frequencies the schedule skipped. | Skipped intervals are blank, quiet ones dotted, activity denser; the footer's coverage matches; OK cycles bursts / strength / legend; Back leaves. |
| U8 | Settings → Battery saver on; open the Scanner, Receive and the Hopper, and run the Activity Logger for 30 min. | The screens update about twice a second; the log has the same rows as without it. With a meter or the battery percentage over hours, note the drain with and without (no saving is claimed until measured). |
| U9 | About → Save report, after a few recordings and opening several tools. | `diagnostics/RG_DIAG_<time>.txt` holds the version, firmware, memory, responsiveness, module and recording figures, and no file names or frequencies. |
| U10 | Full edition: open the Bitstream Explorer on a long RAW recording; open About. | Opens as before (the analysis now runs in its module); About shows the module loads and the slowest. |

## Catalog edition transmit check (Official)

| ID | Check | Pass when |
|----|-------|-----------|
| C1 | Replay a capture on a frequency your region does not allow (on a US device, for example, a 868.35 MHz RAW file). | The Replay screen shows `TX NOT ALLOWED: Region XX does not allow TX on 868.35 MHz` before Send; Send shows `TX refused` with the same text, the error tone, and nothing is transmitted (check with a second receiver). |
| C2 | Replay on an allowed frequency (only where authorized). | `Transmitting`, then `Signal sent`. |
| C3 | If you have a device without a provisioned region (`--` in About; for example after a factory reset before the region is fetched). | Every Send is refused with `Firmware has no region info, so TX is off`. |

## Memory and repeated use (beta 6, beta 7)

| ID | Check | Pass when |
|----|-------|-----------|
| M1 | Launch the Full edition right after a reboot and open About. | Launches (no `Out of memory`); note `At app start` and the largest free block, and compare with the Catalog edition on Official if you have both. |
| M2 | Enter and leave the Range Scanner ten times with 256 points, then the Scanner, Receive and the Hopper ten times each, with a device log open; open About. | No crash; `Free now` returns close to `At app start`. |
| M3 | With a 256-point range, open the Range Scanner, then Receive from it, then back, five times. | No `Not enough memory` on a freshly started app; no crash. |
| M4 | Full edition: open and close each module screen (Waterfall, Bitstream Explorer, a comparison, Unknown Protocol Analysis, Sessions) five times each, then open About. | No crash; `Free now` returns close to `At app start` (modules are freed on close). |
| M5 | Full edition, low memory: start the app with another large app's memory in use (or with `Radio_heap` raised as in F15b), open the Waterfall and Sessions. | `Not enough memory` with Back returning, never a crash. |

## Per-firmware load check

Install each build on its matching firmware; installing the wrong one should
produce an API or missing-imports error rather than a crash (see
[Troubleshooting](TROUBLESHOOTING.md#the-app-wont-open)).

| ID | Check | Pass when |
|----|-------|-----------|
| L1 | `radiogeddon-catalog-official.fap` on Official firmware 1.4.x. | Launches as `RadioGeddon`. |
| L2 | `radiogeddon-full-roguemaster.fap` on RogueMaster. | Launches as `RadioGeddon Full`. |
| L3 | `radiogeddon-full-momentum.fap` on Momentum mntm-012. | Launches as `RadioGeddon Full`; W2, F1 and R2 work. |
| L4 | `radiogeddon-full-unleashed.fap` on Unleashed unlshd-093. | Launches as `RadioGeddon Full`. |

## Results log

Copy this into a hardware-report issue and fill it in:

| ID | Firmware + version | Pass/Fail | Notes (observed behaviour, log excerpt) |
|----|--------------------|-----------|------------------------------------------|
| W1 | | | |
| W2 | | | |

When results come in, [VERIFICATION.md](VERIFICATION.md) is updated to move
confirmed items into its evidence-backed table.
