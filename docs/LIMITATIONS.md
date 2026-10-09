# Hardware and API Limitations

## Build & verification status

- **Builds:** Yes. `ufbt` produces `dist/radiogeddon.fap` with no warnings
  against the official release SDK (firmware 1.4.3, target `f7`, API 87.1).
- **Host unit tests:** Pass (`make -C test check`).
- **On physical hardware:** **Not yet verified in this environment.** No Flipper
  Zero was attached during development. Per the project's integrity rules, this
  document does not claim hardware testing passed when it has not.

### Recommended on-device verification (milestone)

1. Install the `.fap` (`ufbt launch` with the device connected, or copy to
   `apps/Sub-GHz/`).
2. Open **RadioGeddon → Receive & Record** on `433.92 MHz`, preset `AM 650`.
3. Trigger a known simple remote (e.g. a Princeton/CAME gate remote, or another
   Flipper transmitting a saved static signal).
4. Confirm the protocol appears in the decoded list, press **OK** to save, name
   it.
5. Alternatively press **Left** to RAW-record, release, and save.
6. Go to **Database**, reopen the file, and confirm **Signal Info & Analysis**
   shows the correct frequency, protocol and details.

If any step misbehaves, capture the serial log (`ufbt cli` → `log`) and file an
issue; likely areas are listed below.

## Known hardware/API constraints

- **Radio chip:** Uses the internal CC1101 via the generic `subghz_devices_*`
  API. External modules are supported by the API but not surfaced in the menu
  yet.
- **RAW capture size:** Captures are buffered in RAM, capped at 16,384 signed
  timing samples (~64 KB). Longer transmissions are truncated; the UI shows a
  `FULL` indicator. This keeps heap usage bounded on the device.
- **Scanner cadence:** The scanner probes one frequency per ~100 ms UI tick with
  a short AGC settle delay, so a full sweep of the frequency table takes a
  couple of seconds. RSSI is a snapshot, not a continuous peak-hold.
- **Transmission:** Governed entirely by firmware region settings
  (`furi_hal_region` / device validity checks). If your region forbids a
  frequency, TX returns a "Blocked by region" result and nothing is sent.
  Dynamic/rolling-code protocols are refused by design.
- **Decoders:** Protocol identification is only as good as the firmware's
  built-in decoder registry and (for KeeLoq manufacturer names) the optional
  `keeloq_mfcodes` keystore, loaded best-effort if present on the SD card.
- **Crypto analysis:** Classification and entropy hints are heuristic signal
  characterization. The tool does **not** and will not recover keys, decrypt
  payloads, or predict rolling codes.

## Firmware-fork compatibility

The application avoids fork-specific APIs. It should build on RogueMaster and
similar forks that preserve the public Sub-GHz interface. If a fork renames or
removes `subghz_devices_*`, `subghz_worker_*`, `subghz_receiver_*`,
`subghz_file_encoder_worker_*`, or the RAW protocol symbols, the corresponding
module will need a compatibility shim.
