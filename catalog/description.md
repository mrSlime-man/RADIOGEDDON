# RadioGeddon

A standalone Sub-GHz research and signal analysis toolkit that runs entirely on the Flipper Zero, with no phone, computer or network. It listens with the built-in CC1101 radio (or an external CC1101 module), records what it hears and helps you understand it.

## What it does

- **Scanner:** live signal strength over a list of common frequencies, with a noise floor, peak hold and activity counter per frequency, and CSV export.
- **Frequency Hopper:** cycles through a list of frequencies, holds on activity and can record each burst automatically.
- **Receive and identify:** decodes known protocols with the firmware's own decoders, so names and keys match the stock Sub-GHz app.
- **RAW capture and recording:** streams RAW recordings straight to the SD card, so their length is limited by the card, not by RAM. Lost samples are counted, never hidden.
- **Decode with Firmware:** runs the firmware's decoders over a saved RAW capture.
- **Signal analysis:** pulse timing, an encoding guess with its confidence, frame structure and a map of what changes between frames. Every result is labelled as confirmed, measured or inferred.
- **Pulse timeline:** a zoomable, scrollable view of a RAW capture.
- **Comparison:** field-by-field and timing comparison of two recordings, static vs rolling-code classification.
- **Signal database:** browse, sort, search, analyse, compare, rename and delete saved signals.
- **Recovery and diagnostics:** a recording cut off by a power loss is repaired and offered back, saves are crash-safe, and About shows memory, recording and radio diagnostics and can save them as a report.
- **Replay:** sends a saved signal again, only where the firmware's region allows transmitting on its frequency, and never decoded rolling-code protocols.

## Transmitting

Before any transmission the app checks that the firmware knows its region, that the region allows the frequency and that the radio accepts it. If not, the app refuses and says why on screen. There is no way to pick another region in the app. RadioGeddon does not recover keys, decrypt signals or predict rolling codes.

## Files

Recordings are standard .sub files in apps_data/radiogeddon/signals on the SD card, so the stock Sub-GHz app can open them too.

## Compatibility

This is the Catalog edition of RadioGeddon, built for the Official firmware.

Source code, documentation and bug reports: https://github.com/mrSlime-man/RADIOGEDDON
