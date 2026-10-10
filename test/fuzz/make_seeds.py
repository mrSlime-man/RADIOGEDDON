#!/usr/bin/env python3
"""Write the seed corpus for the fuzz targets in test/fuzz/corpus/.

The seeds are synthetic: the repository's test fixtures plus hand-made edge
cases (extreme values, CRLF line ends, commas between values, the recorder's
"# Lost:" note, cut-off lines). None of it is a capture from real hardware. Inputs the fuzzer finds
that broke an invariant are added next to them as regression cases.

    python3 test/fuzz/make_seeds.py
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "..", "fixtures")
CORPUS = os.path.join(HERE, "corpus")


def fixture(name):
    with open(os.path.join(FIXTURES, name), "rb") as f:
        return f.read()


def write(target, name, data):
    folder = os.path.join(CORPUS, target)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, name), "wb") as f:
        f.write(data)


RAW = fixture("raw_ref.sub")
KEY_A = fixture("princeton_ref_a.sub")
KEY_B = fixture("princeton_ref_b.sub")
RAW_HEAD = (
    b"Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n"
    b"Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\n"
)

RAW_CASES = {
    "fixture": RAW,
    "crlf": RAW.replace(b"\n", b"\r\n"),
    "lost": RAW + b"# Lost: 1234\n",
    "extremes": RAW_HEAD + b"RAW_Data: 2147483647 -2147483648 99999999999 -0 1 -1 0 7\n",
    "cut": RAW_HEAD + b"RAW_Data: 400 -1200 40",
    "garbage": RAW_HEAD + b"RAW_Data: 400 x -1200 --5 +3 400\nRAW_Data:\n",
    "commas": RAW_HEAD + b"RAW_Data: 1718, -32700, 32700,-494 1047 ,\nRAW_Data: 6, ,7\n",
    "long_line": RAW_HEAD + b"RAW_Data:" + b" 350 -700" * 900 + b"\n",
}

# fuzz_raw: [source read size][reader step] then the file.
for i, (name, text) in enumerate(sorted(RAW_CASES.items())):
    write("fuzz_raw", name, bytes([(7 * i + 1) % 256, 13 * i % 64]) + text)


def record(name, header, whole, mtime):
    head1 = min(len(header) // 4, 0x7F) | (0x80 if whole else 0)
    return bytes([len(name), head1]) + name + header[: (head1 & 0x7F) * 4] + struct.pack(">I", mtime)


# fuzz_db: name/header/time records, then one RAW_Data line.
write(
    "fuzz_db",
    "fixtures",
    bytes([5])
    + record(b"gate.sub", KEY_A, True, 1760000000)
    + record(b"Gate copy.sub", KEY_A, True, 1760000100)
    + record(b"other.sub", KEY_B, True, 1750000000)
    + record(b"raw.sub", RAW, False, 1740000000)
    + record(b"bad.sub", b"not a sub file\n", True, 0)
    + b"400 -1200 1200 -400 -2147483648 99999999999999999999 -99999999999999999999",
)
write(
    "fuzz_db",
    "names",
    bytes([4])
    + record(b".hidden", b"", True, 1)
    + record(b"a:b?.SUB", KEY_A[:40], False, 2)
    + record(b" edge. ", KEY_B, True, 3)
    + record(b"x" * 70, RAW, True, 4)
    + b"0 0 -0 5",
)

# fuzz_samples: 8 view bytes, then little-endian int32 samples.
values = []
for line in RAW.splitlines():
    if line.startswith(b"RAW_Data:"):
        values += [int(v) for v in line.split()[1:]]
write("fuzz_samples", "fixture", bytes([3, 0, 0, 0, 0, 0, 4, 1]) + struct.pack("<%di" % len(values), *values))
write(
    "fuzz_samples",
    "extremes",
    bytes([9, 255, 255, 255, 255, 255, 31, 7])
    + struct.pack("<8i", 2147483647, -2147483648, 0, 1, -1, 2147483647, 2147483647, -2147483648),
)
pwm = [350, -1050, 1050, -350] * 12 + [350, -11000]
write("fuzz_samples", "pwm_frames", bytes([0, 0, 0, 0, 0, 0, 1, 2]) + struct.pack("<%di" % (len(pwm) * 3), *(pwm * 3)))
