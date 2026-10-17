#!/usr/bin/env python3
"""What loading each Full-edition module takes from the heap, and the free
block the app asks for before loading it (beta 10's rule and beta 11's).

    scripts/module_cost.py DIR_OR_FAL...            table
    scripts/module_cost.py --check TOOL DIR_OR_FAL...

Reads each .fal's section table the way helpers/rg_elf.c does (a model of
the firmware 1.4.3 loader, lib/flipper_application/elf): allocated sections
in aligned blocks of their own (resident), .fast.rel.* tables loaded with
them and freed after relocation (temporary), .ARM.* skipped. --check runs
the C model (test/elf_cost, built with `make -C test build/elf_cost`) on
the same files and fails unless both agree. The work figures are the
tool scenes' work_kb (scenes/radiogeddon_scene_tools.c) and the analysis
modules' own requirements, given here by module.

These are computed requirements, not measurements on a device.
"""
import argparse
import glob
import os
import struct
import subprocess
import sys

SHF_ALLOC = 0x2
BOOK = 64  # RG_ELF_SECTION_BOOK
LOADER = 2048  # RADIOGEDDON_MODULE_LOADER (beta 11)
LOADER_B10 = 6144  # beta 10's flat allowance
MARGIN = 4096

# Work (bytes) the largest screen of each module asks for besides the module,
# from scenes/radiogeddon_scene_tools.c (work_kb) on the device's sizes.
WORK_KB = {
    "radiogeddon_fp.fal": 16,
    "radiogeddon_studio.fal": 20,
    "radiogeddon_reports.fal": 14,
    "radiogeddon_log.fal": 8,
    "radiogeddon_heatmap.fal": 12,
    "radiogeddon_about.fal": 2,
    "radiogeddon_lab.fal": 4,
    "radiogeddon_range.fal": 4,
    "radiogeddon_settings.fal": 1,
    "radiogeddon_hopper.fal": 1,
    "radiogeddon_bits.fal": 15,
}


def block(n):
    return (n + 8 + 7) & ~7


def aligned(size, align):
    return block(size + max(align, 1) - 1 + 4)


def cost(path):
    d = open(path, "rb").read()
    shoff = struct.unpack_from("<I", d, 32)[0]
    entsize, num, strndx = struct.unpack_from("<HHH", d, 46)
    hdrs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * entsize) for i in range(num)]
    names = hdrs[strndx][4]
    sections = resident = temporary = 0
    for h in hdrs[1:]:
        name = d[names + h[0] : d.index(b"\0", names + h[0])].decode()
        flags, size, align = h[2], h[5], h[8]
        if name.startswith((".ARM.", ".rel.ARM.", ".fast.rel.ARM.")):
            continue
        b = aligned(size, align) if size else 0
        if flags & SHF_ALLOC:
            sections += size
            resident += b + BOOK
        elif name.startswith(".fast.rel"):
            temporary += block(24) + b
        elif name == ".gnu_debuglink":
            resident += block(size)
    return sections, resident, temporary


def files(args):
    out = []
    for a in args:
        out += sorted(glob.glob(os.path.join(a, "*.fal"))) if os.path.isdir(a) else [a]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", metavar="TOOL")
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()
    fals = files(args.paths)
    if not fals:
        sys.exit("no .fal files")
    if args.check:
        res = subprocess.run([args.check] + fals, capture_output=True, text=True, check=True)
        c = {line.split()[0]: tuple(map(int, line.split()[1:])) for line in res.stdout.splitlines()}
        bad = [f for f in fals if c.get(os.path.basename(f)) != cost(f)]
        for f in bad:
            print(f"MISMATCH {os.path.basename(f)}: C {c.get(os.path.basename(f))} Python {cost(f)}")
        if bad:
            sys.exit(1)
        print(f"OK   the C and Python load-cost models agree on {len(fals)} modules")
        return
    print(
        f"{'module':26s} {'sections':>8s} {'resident':>8s} {'reloc':>6s} {'work':>6s}"
        f" {'need b10':>9s} {'need b11':>9s} {'diff':>6s}"
    )
    for f in fals:
        s, r, t = cost(f)
        name = os.path.basename(f)
        work = WORK_KB.get(name, 0) * 1024
        b10 = s + LOADER_B10 + work + MARGIN
        b11 = r + LOADER + max(t, work) + MARGIN
        print(f"{name:26s} {s:8d} {r:8d} {t:6d} {work:6d} {b10:9d} {b11:9d} {b11 - b10:+6d}")


if __name__ == "__main__":
    main()
