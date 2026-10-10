#!/usr/bin/env python3
"""Resident size of .fap files: the bytes the loader places in RAM.

Sums every allocated ELF section (SHF_ALLOC: code, read-only data, data and
bss), which is what `arm-none-eabi-size` reports as text + data + bss, and
prints a breakdown. Pure Python, no toolchain needed.

    scripts/fap_size.py FILE.fap [FILE.fap ...]
    scripts/fap_size.py --compare BEFORE.fap AFTER.fap
"""

import argparse
import struct
import sys

SHF_ALLOC = 0x2
SHT_NOBITS = 8


def sections(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        raise SystemExit(f"{path}: not an ELF32 file")
    e_shoff = struct.unpack_from("<I", data, 32)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    hdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, _addr, offset, size = struct.unpack_from("<IIIIII", data, off)
        hdrs.append((name, typ, flags, offset, size))
    str_off = hdrs[e_shstrndx][3]
    out = []
    for name, typ, flags, _offset, size in hdrs:
        end = data.index(b"\0", str_off + name)
        out.append((data[str_off + name : end].decode(), typ, flags, size))
    return out


def resident(path):
    groups = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    for name, typ, flags, size in sections(path):
        if not flags & SHF_ALLOC:
            continue
        if typ == SHT_NOBITS:
            groups["bss"] += size
        elif name.startswith(".text"):
            groups["text"] += size
        elif name.startswith(".rodata"):
            groups["rodata"] += size
        else:
            groups["data"] += size
    return groups


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--compare", action="store_true", help="two files: before, after")
    args = ap.parse_args()
    results = [(f, resident(f)) for f in args.files]
    for f, g in results:
        total = sum(g.values())
        parts = " ".join(f"{k}={v}" for k, v in g.items())
        print(f"{f}: {total} B ({total / 1024:.1f} KB) {parts}")
    if args.compare:
        if len(results) != 2:
            sys.exit("--compare takes exactly two files")
        a, b = (sum(g.values()) for _, g in results)
        print(f"change: {b - a:+d} B ({(b - a) / 1024:+.1f} KB, {100.0 * (b - a) / a:+.1f}%)")


if __name__ == "__main__":
    main()
