# Hardware Verification Checklist

These steps require a physical Flipper Zero and **cannot** be verified in the
build/CI environment. Each item lists how to perform it and what a pass looks
like. Record results in the table at the bottom.

> No Flipper Zero was connected during development, so **every item below is
> currently UNVERIFIED**. This file is the test plan to run on real hardware.

## Setup

- [ ] H0. Build and install: `ufbt launch` with the device connected over USB,
      or copy `dist/radiogeddon.fap` to `apps/Sub-GHz/`.
- [ ] H1. Copy the reference captures from `test/fixtures/*.sub` to
      `/ext/apps_data/radiogeddon/signals/` on the SD card (create the folder or
      let the app create it on first run, then copy via qFlipper).
- [ ] H2. Open a serial log for diagnostics: `ufbt cli` then `log` (leave running
      in a second terminal).

## End-to-end workflow (completion criteria)

1. [ ] **Launch** — App starts from Apps → Sub-GHz → RadioGeddon; main menu
   renders; no crash in the log.
2. [ ] **Start receiver** — Settings → set `433.92 MHz`, `AM 650`. Open
   Receive & Record. RSSI meter moves; "Listening..." shown. (If "No radio"
   appears, the internal CC1101 failed to init — capture the log.)
3. [ ] **Capture a transmission** — Trigger a known remote (e.g. a Princeton/CAME
   gate remote) or transmit `princeton_ref_a.sub` from a second Flipper. The
   decoded protocol appears in the list; decoded count increments.
4. [ ] **Display info** — The decoded protocol name and `[n]` index show on
   screen.
5. [ ] **Save** — Press OK, accept/edit the name, confirm success tone. Also test
   RAW: press Left to record, let a burst in, press Left again, name & save.
6. [ ] **Reopen** — Database → open the saved file. Signal Info & Analysis shows
   correct frequency, protocol, bits/key (protocol) or sample count & pulse
   range (RAW).
7. [ ] **Analyze structure** — For a RAW file, timing groups and estimated base
   `Te` are shown; for a protocol file, `[CONFIRMED]` and bit breakdown.
8. [ ] **Compare** — From a saved signal, Compare with… and pick a second file
   (e.g. `princeton_ref_a` vs `princeton_ref_b`). The `Key` line is marked
   changed (`~`) while `Proto`/`Freq` are constant (`=`).
9. [ ] **Replay (authorized)** — For a RAW or static file, Replay → Send. With a
   permitted region/frequency it transmits (verify on a receiver/second
   Flipper). A dynamic/rolling-code file must report "Protected/rolling code".
   A region-blocked frequency must report "Blocked by region".
10. [ ] **Exit safely** — Back out through every screen to the launcher. No crash,
    no hang; the radio LED is off; the log shows a clean app exit.

## Module-specific checks

- [ ] Scanner sweeps the frequency table; bars update; selecting a frequency with
      OK opens the receiver tuned to it.
- [ ] Frequency Hopper cycles 315 / 390 / 433.92 / 868.35 MHz; the displayed
      frequency changes as it hops; when a transmission starts on one of those
      bands it holds there and decodes it; OK saves a decode; Back exits cleanly
      with the radio released.
- [ ] Crypto Analysis on a static protocol says `[CONFIRMED] Static code`; on a
      KeeLoq-family capture says `[CONFIRMED] Dynamic code` and explicitly states
      no key recovery.
- [ ] Rapid repeated transmissions do not crash; history de-duplicates identical
      consecutive parcels and caps at 32 entries.
- [ ] RAW capture of a long/continuous signal shows the `FULL` indicator at the
      16384-sample cap without crashing.
- [ ] SD card removed mid-session: save/open fail gracefully with an error tone,
      no crash.
- [ ] Memory: run several capture/save/open/replay cycles; free heap (via `log`
      or `free` in CLI) returns to baseline — no growth across cycles.

## Results log

| Date | Firmware | Item | Pass/Fail | Notes |
|------|----------|------|-----------|-------|
|      |          |      |           |       |
