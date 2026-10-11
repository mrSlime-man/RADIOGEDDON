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
| W1 | Launch: Apps → Sub-GHz → RadioGeddon (Catalog) or RadioGeddon Full. | Catalog: the menu lists Scanner, Receive & Record, Frequency Hopper, Database, Settings, About. Full: header `RadioGeddon Full` and Scan, Receive & Record, Analyze, Database, Sessions, Settings, About (Scan holds Frequency Scanner, Range Scanner, Waterfall, Frequency Hopper, Favorites). No error in the log. |
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
