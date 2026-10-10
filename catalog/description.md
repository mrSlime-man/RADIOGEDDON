# RadioGeddon

A Sub-GHz signal analysis toolkit that runs entirely on the Flipper Zero. It listens with the built-in CC1101 radio (or an external CC1101 module), records what it hears and helps you understand it.

## What it does

- **Scanner:** live RSSI over a list of common frequencies, with a noise floor, peak hold and activity counter per frequency, and CSV export.
- **Frequency Hopper:** cycles through frequencies, holds on activity and can record each burst automatically.
- **Receive and record:** decodes known protocols with the firmware's own decoders and streams RAW captures to the SD card.
- **Signal analysis:** pulse timing, encoding guess, frame structure and a field map of what changes between frames, all labelled as measured or inferred.
- **Pulse timeline:** a zoomable view of a RAW capture.
- **Signal database:** sort, search, compare, rename and delete saved signals.
- **Replay:** sends a saved signal again, only where the firmware's region allows transmitting on its frequency, and never rolling-code protocols.

## Transmitting

Before any transmission the app checks that the firmware knows its region and that the region allows the frequency. If it does not, the app refuses and says why on screen. There is no way to pick another region in the app.

## Files

Recordings are standard .sub files in apps_data/radiogeddon/signals on the SD card, so the stock Sub-GHz app can open them too.

Source code and documentation: https://github.com/mrSlime-man/RADIOGEDDON
