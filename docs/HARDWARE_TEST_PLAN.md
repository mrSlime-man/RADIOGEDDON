# Hardware Test Plan

This procedure verifies RadioGeddon on a **physical Flipper Zero**. Everything
in this document requires real hardware and RF signals.

> ## ⚠️ Status: UNVERIFIED on hardware
>
> As of this release, RadioGeddon has been verified only to **compile and pass
> the SDK API-compatibility check** against official firmware (API 87.1) and
> Unleashed (API 88.9). **No test below has been run on a physical device.**
> Every result column reads `UNVERIFIED` until a human runs the test and
> records the outcome. Do not treat any on-device behaviour as confirmed until
> this file (or a linked report) shows real results.

## Legal and safety notice

Receiving is passive and generally fine. **Transmitting (replay) is
regulated.** Only transmit on frequencies and devices you are legally
authorized to operate, ideally into a dummy load or a device you own in an
RF-isolated setting. The replay tests below assume an authorized test signal
you own (e.g. your own garage remote on a bench, or a signal generator).

## Equipment

- Flipper Zero on official, Unleashed, or RogueMaster firmware.
- The matching RadioGeddon `.fap` installed (see INSTALL.md).
- A microSD card inserted and working.
- A **fixed-code** 433.92 MHz OOK test transmitter you own (e.g. a cheap
  fixed-code remote or a bench generator) for detection/replay tests.
- Optional: a second recording of the same button for the Compare test.

## How to record results

Copy the table at the bottom, and for each step fill in **Result**
(PASS / FAIL), the firmware name+version, and notes. Attach photos where
useful. Replace the ⚠️ status block above once a full pass is recorded.

---

## Test 1 — Application startup
1. On the Flipper: **Apps → Sub-GHz → RadioGeddon**.
2. **Expected:** the main menu appears with items: Receiver, RAW Record,
   Frequency Hopper, Saved Signals, Compare Signals, Replay Signal, Settings,
   About. No crash, no watchdog reboot.
3. Navigate the menu with Up/Down; press Back to confirm it exits cleanly to
   the launcher.

## Test 2 — Radio initialization
1. Open **Settings**, confirm Frequency defaults to `433.92` and Modulation to
   `AM650 OOK`. Change and restore each to confirm the lists work.
2. Open **Receiver**.
3. **Expected:** the screen shows the frequency, a live RSSI bar and value,
   and "Listening…". It must **not** show "Radio not available" on real
   hardware. The RSSI value should move when you bring an RF source near.

## Test 3 — Signal detection
1. In **Receiver** on 433.92 / AM650, press your fixed-code test remote nearby.
2. **Expected:** RSSI jumps while the button is held; the "Found" counter
   increments; the green LED blinks on each decode.

## Test 4 — RAW recording
1. Return to the menu, open **RAW Record** on 433.92 / AM650.
2. **Expected:** red LED indicates recording; "Samples" climbs while you press
   the test remote several times.
3. Press **OK** to stop.
4. **Expected:** you are taken to the name entry screen.

## Test 5 — Protocol identification
1. In **Receiver**, trigger a remote whose protocol is supported (e.g. a
   Princeton/CAME/NICE fixed-code remote).
2. **Expected:** the last decoded protocol name is shown; rolling-code remotes
   are annotated `(rolling)`.

## Test 6 — Saving recordings
1. After Test 4, enter a name (e.g. `test1`) and confirm.
2. **Expected:** a "Saved" confirmation; the file exists at
   `/ext/subghz/test1.sub` (verify via qFlipper or the stock Sub-GHz app).

## Test 7 — Loading recordings
1. Menu → **Saved Signals**.
2. **Expected:** the file browser lists `/ext/subghz`; select `test1`.
3. **Expected:** the detail screen shows Type, Frequency, Preset, Protocol and
   RAW sample count, with Delete / Replay / Compare buttons.

## Test 8 — Signal comparison
1. Record the **same** button a second time as `test2` (Tests 4 & 6).
2. Menu → **Compare Signals** → pick `test1`, then `test2`.
3. **Expected:** a high similarity score (typically ≥ 85% for the same
   fixed-code button) and verdict "MATCH".
4. Compare `test1` against a recording of a **different** button.
5. **Expected:** a low score and verdict "DIFFERENT".

## Test 9 — Frequency Hopper operation
1. Menu → **Frequency Hopper**.
2. **Expected:** the displayed frequency cycles through the hopper list
   (315 / 433.92 / 868.35 / 915); RSSI updates; triggering a remote on one of
   those frequencies increments "Found" and lingers briefly on that frequency.
3. Press Back.
4. **Expected:** returns to the menu with the radio stopped (no stuck RX).

## Test 10 — Authorized replay of a supported test signal
> Only with a signal and frequency you are authorized to transmit.
1. Menu → **Saved Signals** → select your fixed-code `test1` → **Replay**
   (or Menu → **Replay Signal** → pick the file).
2. Read the on-air warning, then press **Send**.
3. **Expected:** "Transmitting…" then "Sent"; magenta LED during TX; the
   authorized target device responds appropriately to the fixed code.
4. Repeat with a frequency disallowed by your region.
5. **Expected:** replay is refused with "TX blocked by region" — it must
   **not** transmit.

## Additional reliability checks
- **Long receiver session:** leave **Receiver** running for ≥ 30 minutes with
  intermittent signals. **Expected:** no crash, no memory growth leading to a
  reboot, RSSI still live, decoding still works.
- **Rapid navigation:** enter/exit Receiver, Record and Hopper repeatedly
  (≥ 20 times). **Expected:** no crash and the radio always stops on exit.
- **SD removed mid-use:** (optional, advanced) start a recording, then note
  that pulling the SD should surface "SD write failed" rather than crash.
- **Back during TX:** start an authorized replay and press Back mid-send.
  **Expected:** TX stops immediately and you return to the previous screen.

---

## Results table (fill in on real hardware)

| # | Test | Firmware (name + ver) | Result | Notes |
|---|------|-----------------------|--------|-------|
| 1 | Application startup | | UNVERIFIED | |
| 2 | Radio initialization | | UNVERIFIED | |
| 3 | Signal detection | | UNVERIFIED | |
| 4 | RAW recording | | UNVERIFIED | |
| 5 | Protocol identification | | UNVERIFIED | |
| 6 | Saving recordings | | UNVERIFIED | |
| 7 | Loading recordings | | UNVERIFIED | |
| 8 | Signal comparison | | UNVERIFIED | |
| 9 | Frequency Hopper | | UNVERIFIED | |
| 10 | Authorized replay | | UNVERIFIED | |
| — | Long receiver session | | UNVERIFIED | |
| — | Rapid navigation | | UNVERIFIED | |
| — | SD removed mid-use | | UNVERIFIED | |
| — | Back during TX | | UNVERIFIED | |
