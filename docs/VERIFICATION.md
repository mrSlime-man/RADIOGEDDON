# Verification Status

This records exactly what has been verified and how, and what remains
unverified. It is updated as verification progresses. Integrity rule: nothing
is marked hardware-verified without evidence from a physical device.

## Environment at time of writing

- No Flipper Zero connected (no USB device, no `/dev/ttyACM*`, `ufbt` reports no
  device target). Hardware steps therefore could not be run.

## Verified in this environment (evidence-backed)

| Area | Method | Result |
|------|--------|--------|
| Compilation | `ufbt` against release SDK (fw 1.4.3, target f7, API 87.1) | **Pass**, zero warnings; `dist/radiogeddon.fap` produced (~50 KB) |
| Clean rebuild | `rm -rf .ufbt/build && ufbt` | **Pass** |
| DSP/parse unit tests | `make -C test check` | **Pass**, 31 checks, 0 failures |
| `.sub` RAW serialize↔parse round-trip | host test `test_raw_roundtrip` | **Pass** — 1000-sample array survives chunked emit + reparse with correct count/min/max/clusters |
| Static analysis | `cppcheck --enable=warning,style,performance,portability` | No real defects; only OOM-path false positives (Flipper `malloc` aborts, never returns NULL) and macro-parser noise |
| Radio RX/TX sequencing | Cross-checked against official firmware `subghz_txrx.c` | Corrected to match (see below) |

## Bugs found and fixed during stabilization

1. **RX started incorrectly.** Removed an erroneous `subghz_devices_set_rx()`
   after `start_async_rx` (that call is for FIFO/sync mode and can disrupt
   async timing capture); added `flush_rx`; reordered to `start_async_rx` →
   `worker_start`, matching the firmware. RX stop reordered to
   `worker_stop` → `stop_async_rx` → `idle`/`sleep`.
2. **TX missing region gate.** Added `subghz_devices_set_tx()` with its
   return-value check before `start_async_tx`, exactly as the firmware does, so
   regional restrictions are enforced at the correct point for both RAW and
   protocol transmit.
3. **Replay mutated the saved file.** Protocol replay now deserializes from an
   in-memory copy (with a bounded `Repeat`) instead of writing `Repeat` back
   into the user's stored `.sub`.
4. **Worker overrun callback** received the wrong context pointer (cast of
   `subghz_receiver_reset`); replaced with a correct wrapper. (Fixed earlier.)
5. **Cross-thread GUI writes.** The decoded-signal view is now updated only on
   the UI thread; the radio worker thread merely records into a mutex-protected
   history and posts a custom event.
6. **Scanner/receiver with no radio** now show a clear message and the scanner
   sweep is gated so it never probes a missing device.
7. **Out-of-band frequency guard.** `set_frequency` asserts on invalid bands;
   the probe and RX paths now validate first and fall back instead of crashing.
8. **Saving no longer wipes the session list.** Returning from the save screen
   preserves the receiver's decoded-signal history.
9. **RAW save** now uses the firmware's `flipper_format_write_int32` array
   writer and the exact `Flipper SubGhz RAW File` v1 header, guaranteeing
   format compatibility.
10. **Empty filename** rejected via a minimum input length; **replay result
    popup** auto-returns via a proper callback.

## Frequency Hopper (new in v0.3)

Implemented after stabilization. It reuses the live receive path and the new
`radiogeddon_subghz_rx_retune()` primitive, which retunes without powering the
radio down — the same stop/retune/start cycle the firmware hopper uses. It holds
on a frequency when RSSI rises above the noise floor so a decode can complete.
Build- and static-analysis-verified; its over-the-air behaviour is an item in
the hardware checklist and is **unverified** until run on a device.

## NOT yet verified (requires physical hardware)

The complete end-to-end workflow (launch → receive → display → save → reopen →
analyze → compare → replay → exit) and all radio behaviour. See
[`HARDWARE_CHECKLIST.md`](HARDWARE_CHECKLIST.md) for the full test plan. In
particular, the following can only be confirmed on a device:

- Real reception and live protocol decoding from over-the-air signals.
- RAW capture fidelity and replay producing a working transmission.
- Regional TX enforcement actually blocking disallowed frequencies.
- Long-run memory stability and absence of radio-threading crashes.

## How to update this file

After running `HARDWARE_CHECKLIST.md` on a device, move each confirmed item into
the "Verified" table with the date, firmware version, and a one-line evidence
note (log excerpt or observed behaviour).
