# Frequently Asked Questions

## General

### What is RadioGeddon?

A Flipper Zero app for looking closely at Sub-GHz radio signals: find active
frequencies, capture transmissions, identify known protocols, analyse unknown
ones, compare captures, keep a library on the SD card, and — where you are
authorized — replay compatible signals. It all runs on the Flipper itself.

### Does it need a phone, computer or internet connection?

No. Once the `.fap` is on the SD card, everything runs on the Flipper. A
computer is only needed to copy the file onto the device (or you can copy it to
the SD card directly).

### How is this different from the built-in Sub-GHz app?

The stock app is excellent for reading, saving and sending signals.
RadioGeddon reuses the same firmware radio drivers and protocol decoders (so
identification matches the stock app), and adds analysis on top: pulse-timing
statistics, line-encoding hypotheses for protocols the firmware doesn't know,
constant-vs-changing field maps across repeated presses, side-by-side
comparison, and static-vs-rolling classification. It also includes a
per-frequency RSSI scanner and a frequency hopper.

### Is it finished?

It is a **public beta** (`1.0.0-beta.2`). Every feature is implemented and the
builds pass all automated checks. One tester has reported that the RogueMaster
build launches and works on a physical Flipper Zero, but the hardware checklist
has not yet been worked through and logged. See [VERIFICATION.md](VERIFICATION.md) for exactly
what has and hasn't been tested, and consider helping with the
[hardware checklist](HARDWARE_CHECKLIST.md).

## Installation and compatibility

### Which file do I download?

The one that matches your firmware: `radiogeddon-official.fap`,
`radiogeddon-unleashed.fap`, or `radiogeddon-roguemaster.fap`. See
[Installation](INSTALLATION.md).

### Why are there three different files?

Each firmware family has its own SDK API version, and the Flipper refuses to
run an app built for a different major API version. Building one file per
family, each against that family's own SDK, is the safe way to support all
three. Details: [Firmware Compatibility](FIRMWARE_COMPATIBILITY.md).

### I get "Error: Outdated App", "Outdated Firmware" or "Missing Imports".

You installed the build for a different firmware family, or your firmware is
older than the one the build targets. Install the matching file, or update your
firmware. See [Troubleshooting](TROUBLESHOOTING.md#the-app-wont-open).

### Does it work on Momentum / Xtreme / other forks?

They aren't built or tested. A fork may load one of the three files if its API
version is compatible; otherwise you can build from source against that fork's
SDK ([instructions](FIRMWARE_COMPATIBILITY.md#building-for-your-exact-firmware)).

### Can I use an external CC1101 module?

Not from the menus yet — RadioGeddon uses the Flipper's internal radio. The
radio code uses the firmware's portable device layer, so external-module
support is on the [roadmap](ROADMAP.md).

### How do I know a download is genuine?

Each release lists SHA-256 checksums in `SHA256SUMS`, and every `.fap` carries a
signed build-provenance attestation linking it to the GitHub Actions run and
the exact tagged commit that produced it:

```bash
sha256sum -c SHA256SUMS
gh attestation verify radiogeddon-official.fap --repo mrSlime-man/RADIOGEDDON
```

## Capabilities and limits

### Can RadioGeddon crack rolling codes, recover keys, or decrypt signals?

No. It is deliberately an analysis tool. It can tell you that a signal *looks
like* a rolling-code or encrypted protocol, and show which bits change between
presses, but RadioGeddon itself never recovers keys, predicts codes, decrypts
payloads, or bypasses rolling-code protection, and it refuses to replay decoded
dynamic (rolling-code) protocols. (A RAW capture of any remote is transmitted
exactly as recorded; replaying a rolling-code capture just re-sends one code the
receiver has already seen.) It does show the firmware's own decoder output,
which may use the SD-card manufacturer keystore to identify KeeLoq-family
signals — that identification happens in the firmware, not in RadioGeddon.

### What do `[CONFIRMED]`, `[OBSERVED]`, `[HEURISTIC]` and `[HYPOTHESIS]` mean?

- `[CONFIRMED]` — one of the firmware's protocol decoders matched the signal.
- `[OBSERVED]` — measured directly from a RAW capture's timing (sample counts,
  timing peaks, noise, jitter, frames). A measurement, not an interpretation.
- `[HEURISTIC]` — a judgement derived from signal statistics (for example pulse
  timings or key-byte variety). Useful, but a guess.
- `[HYPOTHESIS]` — an inference from the analysis engine about an unknown
  signal (encoding, framing, bits, field layout). Treat it as a starting point
  for investigation, not as a decode.

See [Protocol Analysis](PROTOCOL_ANALYSIS.md).

### Why won't it replay a signal I captured?

Replay is refused when a **decoded** signal is a dynamic/rolling-code protocol,
when the firmware's region rules don't allow transmitting on that frequency, or
when the file's modulation preset isn't recognised. (A RAW capture skips the
protocol check and is sent as recorded — so a RAW capture of a rolling-code
remote is transmitted, but it only replays one already-used code.) These
safeguards are intentional.
See [Features → Replay](FEATURES.md#authorized-signal-replay).

### Which frequencies and modulations are supported?

The scanner and settings offer a table of common Sub-GHz frequencies between
300 and 925 MHz, and four standard modulation presets (AM270, AM650, FM238,
FM476). What you may *transmit* on is decided by your Flipper's region
settings. Full list: [Features](FEATURES.md).

### Where are my recordings stored? Can I use files from the stock app?

In `/ext/apps_data/radiogeddon/signals` on the SD card, as standard `.sub`
files. You can copy `.sub` files from the stock app's `/ext/subghz` folder into
that directory to analyse or compare them in RadioGeddon, and RadioGeddon's
files open in the stock app too.

## Legal and safety

### Is it legal to use?

Receiving and transmitting radio signals is regulated, and the rules differ by
country. Use RadioGeddon only with devices you own or are explicitly authorized
to test, and only transmit where your local rules allow it. The firmware's
region settings stay in force, but you remain responsible for complying with
the law.

### I found a security problem. Where do I report it?

Privately, as described in [SECURITY.md](../SECURITY.md) — not in a public
issue.

## Contributing

### How can I help?

The most valuable contribution right now is running the
[hardware checklist](HARDWARE_CHECKLIST.md) on a real Flipper and reporting
the results. Code, documentation and analysis improvements are welcome too —
see [CONTRIBUTING.md](../CONTRIBUTING.md).
