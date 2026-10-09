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
| F5 | History: send rapid repeated transmissions. | No crash; identical consecutive parcels listed once; list caps at 32. |
| F6 | RAW capture of a long/continuous signal. | `FULL` indicator at the cap (16,384 samples, or less when memory is short); no crash. |
| F7 | Custom-preset replay: a stock-app RAW `.sub` saved with a custom preset. | Replays on the correct modulation, or is cleanly refused (`Unsupported file`) — never sent on the wrong modulation. |
| F8 | Delete: choose Delete on a saved file. | Confirmation shown; Cancel/Back keeps the file; Delete removes it. |
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
