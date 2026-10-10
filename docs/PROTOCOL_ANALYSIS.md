# Protocol Analysis

How RadioGeddon identifies and analyses signals, what every part of the
on-screen reports means, and — just as important — what the analysis cannot
tell you.

> **Status:** the analysis code is covered by host unit-test checks (55 for parsing and the analysis engine) on
> synthetic signals. It has **not** yet been validated against real captures on
> a physical Flipper Zero ([VERIFICATION.md](VERIFICATION.md)).

## Confidence labels

Every conclusion in a report carries one of three labels:

| Label | Meaning | Source |
|-------|---------|--------|
| `[CONFIRMED]` | A firmware protocol decoder recognised the signal, or the firmware's protocol registry classifies the protocol. | Flipper firmware decoders and protocol registry |
| `[HEURISTIC]` | A judgement from simple signal statistics (pulse widths, key-byte variety, key differences). | RadioGeddon |
| `[HYPOTHESIS]` | An inference about an unknown signal from the analysis engine — encoding, framing, bits, field layout. | RadioGeddon signal engine |

`[HEURISTIC]` and `[HYPOTHESIS]` results are starting points for your own
investigation, never verified decodes.

## Protocol identification — `[CONFIRMED]`

While **Receive & Record** or the **Frequency Hopper** is running, every
received pulse is fed to the firmware's own protocol decoders (all protocols
the firmware marks as *decodable*, e.g. Princeton, CAME, Nice FLO, Holtek and
KeeLoq-family protocols — the exact list depends on your firmware). When one
matches, the decoded parcel appears in the list and can be saved as a standard
`.sub` file containing the protocol name, bit count and key.

Two points to keep in mind:

- Identification is exactly as good as your firmware's decoders. KeeLoq-family
  manufacturer names are only resolved if the firmware's manufacturer keystore
  is present on the SD card; RadioGeddon loads it on a best-effort basis.
- When you reopen a saved protocol file, RadioGeddon reports the protocol that
  is **recorded in the file** — it does not re-decode the signal. Files you
  wrote yourself carry the decoder's original result; treat `.sub` files from
  other sources with the same caution as any file you didn't create.

## Signal Info & Analysis

Opening a recording from the **Database** and choosing *Signal Info &
Analysis* shows a summary followed by an analysis section.

**Summary** — name, protocol (`RAW` for raw captures), frequency, modulation
preset, and either the bit length and key (decoded protocols) or the number of
timing samples and the shortest–longest pulse (RAW captures).

**Decoded protocols** — `[CONFIRMED] <protocol>`, the bit length and byte
count, the key in hex, and the key's lowest four bits with a `[HEURISTIC]`
note that low bits often encode the button or command. Whether that is true
depends on the protocol.

**RAW captures** — `[HEURISTIC] RAW timing` statistics over every sample in the
file:

- *Edges*: number of non-zero timing samples.
- *Timing groups*: durations grouped when they are within 20 % (+10 µs) of a
  group's running average, up to 8 groups, listed as `<width> us x<count>`.
- *Est. base Te*: the shortest group's width — a first guess at the protocol's
  base time unit.

(*Unknown Protocol Analysis* groups timings a little differently — 25 % vs. 20 %
tolerance, and it ignores tiny groups — so it may report a slightly different Te
for the same file.)

On-off keyed (OOK) remotes typically show two or three dominant groups (for
example ~400 µs and ~1200 µs, a 1:3 ratio). Many tight groups usually mean
noise or several overlapping transmitters.

## Unknown Protocol Analysis — `[HYPOTHESIS]`

For RAW captures the firmware could not decode, *Unknown Protocol Analysis*
runs RadioGeddon's signal engine (`helpers/rg_analyzer.c`) over the **first
4,096 timing samples** of the file. Each step is a hypothesis:

1. **Timing groups and base Te.** Durations are grouped within 25 % (+10 µs),
   up to 10 groups. Te is the shortest group that holds at least 5 % of all
   edges, so stray glitches don't define the time base.
2. **Frames.** Any gap (carrier-off period) of at least **7 × Te** ends a
   frame; up to 64 frames are tracked.
3. **Repeats.** Remotes usually send the same frame several times per press.
   Frames whose length matches the most common length (±1 sample) count as
   repeats; one of them becomes the *representative frame* (otherwise the
   longest frame is used).
4. **Encoding hypothesis** on the representative frame, with a confidence
   score:

   | Hypothesis | When | Confidence |
   |------------|------|------------|
   | **PPM (gap-coded)** | one dominant pulse width, two or more gap widths | 55 %, +20 if most durations are ≈ Te; max 90 % |
   | **PWM / OOK** | two or more pulse widths, or one pulse width with ≥ 15 % of durations ≈ 2 × Te | 50 % + coverage of Te/2Te; max 92 % |
   | **Manchester** | ≥ 85 % of durations ≈ Te or 2 × Te (≥ 25 % ≈ Te and ≥ 15 % ≈ 2 Te) | 45 % + excess coverage; max 80 % |
   | **PWM / OOK (weak)** | none of the above | 25 % |

   ("Dominant" = at least 10 % of the high or low durations.) In practice the
   PWM/OOK branch is tried first and catches most on-off-keyed remotes; the
   Manchester branch rarely fires, and noisy or non-OOK input usually lands on
   the 25 % "weak PWM" fallback — treat a low confidence as "no clear
   structure", and the extracted bits of a weak result as meaningless.
5. **Bits (PWM only).** Starting at the first carrier-on pulse, each on/off pair
   becomes one bit: `1` if the on-time is at least as long as the off-time,
   otherwise `0`. Up to 256 bits. The polarity is a convention — some protocols
   use the opposite one.
6. **Constant vs. changing fields.** When repeated frames exist, each one is
   converted to bits and compared position by position with the representative
   frame. The map shows `.` for positions that never change and `X` for
   positions that do, with the counts of each.
7. **Device ID candidate.** The constant bits are packed (up to 64) into a hex
   value. A fixed device or remote identifier often lives in the constant part
   of a frame — but this is only a candidate.

### Reading the field map correctly

- The comparison is between frames **inside one recording**. A single button
  press normally repeats an identical frame, so even a rolling-code remote shows
  `0` changing bits from one press. To see which fields change between presses,
  **press the button several times during one RAW recording**.
- A few `X` positions in otherwise constant frames are often noise or a frame
  boundary detected one sample off, not a counter.
- When everything is constant across several presses, the report says the
  signal is *likely a fixed code* — still a hypothesis.

## Crypto Analysis

*Crypto Analysis* describes how a **decoded** protocol protects itself. It
never recovers keys or predicts codes.

- **Classification** comes from the firmware's protocol registry:
  - `[CONFIRMED] Static code` — the protocol sends the same code every press;
    no counter, no encryption.
  - `[CONFIRMED] Dynamic code` — a rolling-code protocol (KeeLoq-style): the
    payload changes every press and is likely encrypted. The report states that
    key recovery is not performed.
  - `[CONFIRMED] Telemetry` — weather-station/sensor protocols, usually
    unencrypted data frames.
  - `[HEURISTIC] Unclassified` or *not in registry* when the firmware has no
    classification for the name in the file.
- **Key-byte variety** — `[HEURISTIC]` counts of non-zero and distinct bytes in
  the key; six or more distinct bytes adds the note *possibly encrypted*.
- **RAW captures** can't be classified (there is no decoded protocol). Use
  *Unknown Protocol Analysis* across several presses, or *Compare*, to look for
  changing fields.

## Signal Comparison

*Compare with…* shows two recordings side by side: `=` marks a field that is
the same, `~` a field that differs.

- **All files:** protocol and frequency.
- **Decoded protocols:** the key. When both files use the same protocol and the
  keys differ, a `[HEURISTIC] key delta` (difference of the lower 32 bits) is
  shown with the hint that a *small* delta across presses suggests a counter.
  The hint is printed whenever the keys differ, so judge the size of the delta
  yourself — encrypted rolling codes produce large, random-looking deltas.
- **RAW captures:** the sample counts, plus a **RAW timing match** score:
  - the first 4,096 samples of each file are compared position by position;
  - two samples match when they have the same polarity and differ by at most
    25 % of the longer one (minimum 60 µs);
  - the score is matches ÷ the longer capture's length, so length differences
    lower the score;
  - ≥ 90 % → *near-identical capture*, ≥ 60 % → *similar structure*,
    otherwise *clearly different*.

  The comparison does **not** align the captures. Two recordings of the same
  button that start at different points in the transmission (or with different
  amounts of noise before it) can score low. Scores are most meaningful when both captures were started
  the same way (for example, recording started just before pressing the button).

## What RadioGeddon never does

- Recover, guess or brute-force cryptographic keys.
- Decrypt payloads or predict the next rolling code. (RadioGeddon performs no
  decryption of its own. The firmware's KeeLoq-family decoders, whose text it
  displays, may use the SD-card manufacturer keystore to identify a signal and
  show its counter — that happens in the firmware.)
- Replay *decoded* dynamic (rolling-code) protocols. (A RAW capture is replayed
  as recorded, which only re-sends one already-used code.)
- Present an engine inference as a confirmed decode.

## Trying it with reference captures

`test/fixtures/` contains reference `.sub` files you can copy to
`/ext/apps_data/radiogeddon/signals/`:

| File | Use |
|------|-----|
| `princeton_ref_a.sub`, `princeton_ref_b.sub` | Decoded Princeton (static, 24-bit) signals with different keys — try *Crypto Analysis* and *Compare*. |
| `raw_ref.sub` | A short OOK-like RAW capture with two repeated frames — try *Signal Info & Analysis* and *Unknown Protocol Analysis*. |

These are synthetic test files, not recordings of real devices.
