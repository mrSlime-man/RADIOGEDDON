#!/usr/bin/env python3
"""Sizes of the app's heap objects as the device compiler lays them out.

    UFBT_HOME=<sdk home> scripts/struct_sizes.py [--root DIR] [TYPE ...]

Host `sizeof` is wrong for the Flipper (64-bit pointers, other alignment), so
this compiles a probe with the SDK's own arm-none-eabi-gcc and the exact
flags ufbt uses for radiogeddon.c (from `ufbt cdb`), one object per type, and
reads each size back from the object's symbol table. Nothing is linked or
run. With no TYPE the default list below is measured; --root measures another
copy of the app (the Full edition staged by scripts/stage_edition.py).
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile

DEFAULT = [
    "RadioGeddonApp",
    "RgAnalyzer",
    "RgAnalysis",
    "RgRawReader",
    "RadioGeddonRawFile",
    "RadioGeddonSettings",
    "RadioGeddonLoadedSignal",
    "RadioGeddonHistory",
    "RgDb",
    "RgDbEntry",
    "RgFp",
    "RgFpBuilder",
    "RgFpRecord",
    "RgRecoverScan",
    "RgActlog",
    "RgHeatmap",
    "RgJournal",
    "RgMemStat",
]
HEADERS = [
    "radiogeddon.h",
    "helpers/rg_analyzer.h",
    "helpers/rg_raw.h",
    "helpers/rg_db.h",
    "helpers/rg_fp.h",
    "helpers/rg_actlog.h",
    "helpers/rg_journal.h",
    "helpers/rg_memstat.h",
    "helpers/rg_recover.h",
    "helpers/rg_heatmap.h",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("types", nargs="*")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    if not os.environ.get("UFBT_HOME"):
        sys.exit("set UFBT_HOME to an installed SDK")
    subprocess.run(["ufbt", "cdb"], cwd=root, check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(root, ".vscode", "compile_commands.json")) as f:
        entries = json.load(f)
    entry = next(e for e in entries if e["file"].endswith("radiogeddon.c"))
    cmd = shlex.split(entry["command"])
    gcc = cmd[0]
    flags = [a for i, a in enumerate(cmd[1:], 1) if a != "-c" and cmd[i - 1] != "-o" and a != "-o"]
    flags = [a for a in flags if not a.endswith("radiogeddon.c")]
    types = args.types or DEFAULT
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "probe.o")
        sizes = {}
        # One probe per type, so a type missing from this edition or tree
        # is reported as "-" instead of failing the others.
        for t in types:
            one = os.path.join(tmp, "one_%s.c" % t)
            with open(one, "w") as f:
                for h in HEADERS:
                    if os.path.exists(os.path.join(root, h)):
                        f.write('#include "%s"\n' % os.path.join(root, h))
                f.write("__attribute__((used)) char sz_%s[sizeof(%s)];\n" % (t, t))
            r = subprocess.run(
                [gcc, *flags, "-Wno-error", "-c", one, "-o", obj],
                cwd=entry["directory"],
                capture_output=True,
                text=True,
            )
            if r.returncode != 0:
                sizes[t] = None
                continue
            nm = os.path.join(os.path.dirname(gcc), "arm-none-eabi-nm")
            out = subprocess.check_output([nm, "-S", "-t", "d", obj], text=True)
            for line in out.splitlines():
                parts = line.split()
                if len(parts) == 4 and parts[3] == "sz_" + t:
                    sizes[t] = int(parts[1])
    for t in types:
        print("%-28s %s" % (t, "-" if sizes.get(t) is None else "%d B" % sizes[t]))


if __name__ == "__main__":
    main()
