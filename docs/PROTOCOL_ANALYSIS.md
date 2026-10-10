# Protocol Analysis

How RadioGeddon identifies and analyses signals, what every part of the
on-screen reports means, and — just as important — what the analysis cannot
tell you.

> **Status:** the analysis code is covered by host unit-test checks (149 for
> parsing, the RAW reader and the analysis engine) on synthetic signals. It has
> **not** yet been validated against real captures on a physical Flipper Zero
> ([VERIFICATION.md](VERIFICATION.md)).

## Confidence labels

Every conclusion in a report carries one of four labels:

| Label | Meaning | Source |
|-------|---------|--------|
| `[CONFIRMED]` | A firmware protocol decoder recognised the signal, or the firmware's protocol registry classifies the protocol. A verified protocol decode. | Flipper firmware decoders and protocol registry |
| `[OBSERVED]` | A structural observation measured directly from the RAW timing: sample counts, duration, timing peaks, noise share, jitter, frames cut on gaps. No interpretation. | RadioGeddon signal engine |
| `[HEURISTIC]` | A judgement from simple signal statistics (pulse widths, key-byte variety, key differences). | RadioGeddon |
| `[HYPOTHESIS]` | A statistical inference about an unknown signal: encoding, bits, bit length, repeated patterns, field layout, ID candidate. It may be wrong. | RadioGeddon signal engine |

`[HEURISTIC]` and `[HYPOTHESIS]` results are starting points for your own
investigation, never verified decodes. `[OBSERVED]` values are measurements,
but what they mean is still up to you.

*Save report to SD* in a file's menu writes the same reports, labels
included, to `apps_data/radiogeddon/reports/<name>.txt`, headed by a legend
of these four labels.

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

(*Unknown Protocol Analysis* uses a different method, histogram peaks over the
whole file, so it may report a slightly different Te for the same file.)

On-off keyed (OOK) remotes typically show two or three dominant groups (for
example ~400 µs and ~1200 µs, a 1:3 ratio). Many tight groups usually mean
noise or several overlapping transmitters.

## Unknown Protocol Analysis — `[OBSERVED]` and `[HYPOTHESIS]`

For RAW captures the firmware could not decode, *Unknown Protocol Analysis*
runs RadioGeddon's signal engine (`helpers/rg_analyzer.c`) over the **whole
file**. The file is streamed up to three times through a 256-byte buffer
(`helpers/rg_raw.c`), so a long recording is never loaded into RAM: the
engine needs about 8 KB however long the capture is, and shows *Not enough
free memory* instead of starting when the heap is short.

The report opens with what the labels mean, then has three parts.

### `[OBSERVED]` timing

1. **Samples and duration.** Consecutive values with the same level are merged
   into one pulse first (some recorders split long pulses).
2. **Timing peaks.** High (carrier-on) and low (carrier-off) durations go into
   two histograms with log-spaced bins 10 % wide. A peak is a local maximum
   that falls to half its height on both sides, holds at least 2 % of that
   side's edges and spans at most 8 bins (about ±45 %). Peaks are listed as
   `<width> x<count>`.
3. **Noise** is the share of edges outside every peak. Receiver noise is
   spread over many bins, so it lands here rather than forming peaks.
4. **Jitter** is the mean distance of peak edges from their peak's centre, as a
   percentage (approximate: it is measured per bin).
5. **Quality:** *good* when noise ≤ 15 % and jitter ≤ 10 %, *poor* when noise
   > 50 %, jitter > 25 % or no peak was found, otherwise *fair*.
6. **Frames.** A low at least **7 × Te** long (and at least 1 ms) ends a frame.
   Bursts of fewer than 8 pulses are counted as *bursts* (noise), not frames.
7. **Lost while recording.** If the recorder had to drop samples because the
   SD card fell behind, the file ends with a `# Lost: N` note and the report
   says so. Timing is not continuous where samples are missing, so a frame
   spanning a gap can decode wrongly.

### `[HYPOTHESIS]` structure

1. **Base Te.** The shortest high peak and the shortest low peak that hold at
   least a quarter of their side's largest peak. Receivers tend to widen
   pulses and shorten gaps by the same amount, so when the two are within
   1.6× of each other Te is their average; otherwise the shorter one.
2. **Encoding by trial decoding.** Every frame is decoded three ways, each
   giving a *fit*: the share of symbols that obey that encoding's rules.

   | Encoding | Symbols | A symbol fits when | Bit value |
   |----------|---------|--------------------|-----------|
   | **PWM** | pulse + gap pairs | the pulse is within 30 % of one of the two pulse-width peaks and pulse + gap is within 25 % of their sum | `1` = long pulse |
   | **PPM** | pulse + gap pairs | the pulse is within 30 % of the main pulse width and the gap within 30 % of one of the two in-frame gap peaks | `1` = long gap |
   | **Manchester** | half-bit cells: a duration of 0.5–1.5 Te is one cell, 1.5–2.6 Te two | the two cells of a bit differ in level (both cell phases are tried); other durations count as errors | `1` = low-to-high (IEEE 802.3; the G.E. Thomas convention inverts every bit) |

   A lone pulse before the frame gap is treated as a stop/sync pulse, not a
   bit. Frames where some encoding fits at least 60 % are *signal frames*;
   the encoding with the highest mean fit over them wins and the runner-up is
   shown as *Alt* when it fits at least 30 %.
3. **Confidence** starts at the winner's mean fit, loses 20 points when the
   runner-up is within 10 (10 points when within 25), loses 10 when only one
   frame fits any encoding, gains 5 when a pattern repeats exactly, is capped at
   40 for frames under 8 bits and never exceeds 95 %.
4. **Bit length** is the most common bit count among signal frames.
5. **Patterns.** Identical frames are grouped (A, B, … largest first, up to 6
   patterns) and shown in binary and hex. A frame that matches a pattern after
   shifting up to 4 bits (for example the first frame of a recording that
   started mid-transmission) joins it as *shifted*.
6. **Field map.** Every frame of the modal bit length is compared position by
   position with the reference frame: `.` never changes, `X` changes.
7. **Device ID candidate.** When some bits change, the longest run of at least
   8 constant bits is offered as a possible device ID, in hex. A fixed device
   or remote identifier often lives there, but this is only a candidate.

### Frame list

One line per frame (the first 48): start time, bit count, pattern letter,
and the shift when a frame was aligned (`A+3`). Frames that fit no encoding
show as `noise`; frames longer than 520 pulses are decoded up to that point
and marked `long`.

### Reading the field map correctly

- The comparison is between frames **inside one recording**. A single button
  press normally repeats an identical frame, so even a rolling-code remote shows
  `0` changing bits from one press. To see which fields change between presses,
  **press the button several times during one RAW recording**.
- Pressing *different buttons* also changes bits; the report says changing
  bits may be a counter, a button code or encrypted data.
- When everything is constant across several frames, the report says the
  signal is *likely a fixed code*, which is still a hypothesis.

### What it cannot do

- Recognise FSK signals, protocols with more than two symbol widths, or
  encodings other than PWM, PPM and Manchester. Those report *no frame fits*.
- Tell which Manchester convention or bit order the device uses.
- Decode anything: the bits are a structural reading of the timing. Only the
  firmware's decoders (*Receive & Record*) give `[CONFIRMED]` results.

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
- **RAW captures:** the sample counts, plus two RAW comparisons. Both stream
  the files, so their length does not matter.
  - **RAW timing match**: the files are compared sample by sample from the
    start; two samples match when they have the same polarity and differ by
    at most 25 % of the longer one (minimum 60 µs); the score is matches ÷
    the longer capture's length. ≥ 90 % → *near-identical capture*, ≥ 60 % →
    *similar structure*, otherwise *clearly different*. This score does
    **not** align the captures, so two recordings of the same button that
    start at different points can score low.
  - **`[HYPOTHESIS]` patterns**: each file runs through the analysis engine
    and their dominant frame patterns are compared, aligned by up to 4 bits.
    It reports *Same frame pattern*, *Different encodings*, or how many bits
    differ with a `.`/`X` map. This does not depend on when recording started.

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
