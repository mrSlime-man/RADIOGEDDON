#!/usr/bin/env python3
"""Static analysis of the app's device code, compiled exactly as the .fap is.

    UFBT_HOME=<sdk home> scripts/static_analysis.py [--jobs N] [--skip-cdb]

Runs two independent analysers over every app source file listed in the
ufbt compile database (`ufbt cdb`, written to .vscode/compile_commands.json):

1. GCC's -fanalyzer, using the SDK's own arm-none-eabi-gcc and the exact
   build flags. Two of its warnings are turned off because they only report
   that malloc() might return NULL, and on the Flipper it never does (the
   firmware stops with "out of memory" instead):
   -Wanalyzer-possible-null-dereference and -Wanalyzer-possible-null-argument.
2. clang-tidy with the checks in .clang-tidy (clang static analyzer, bugprone,
   CERT, concurrency, misc, performance, portability), the same flags with
   clang targeting arm-none-eabi and the SDK's newlib headers.

Any finding from either fails the run (exit 1). The SDK must already be
installed in UFBT_HOME (scripts/build_target.sh official does that).
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CDB = os.path.join(ROOT, ".vscode", "compile_commands.json")
WORK = os.path.join(ROOT, "build", "static_analysis")

GCC_ANALYZER = [
    "-fanalyzer",
    "-Wno-analyzer-possible-null-dereference",
    "-Wno-analyzer-possible-null-argument",
]
# GCC options clang does not know (they only affect code generation).
GCC_ONLY = {"-mword-relocations", "-Wa,-gdwarf-sections", "-fsingle-precision-constant", "-mlong-calls"}
FINDING = re.compile(r"^(\S+?):(\d+):(\d+): (warning|error): ", re.M)


def app_entries():
    with open(CDB) as f:
        entries = json.load(f)
    inside = [e for e in entries if os.path.realpath(e["file"]).startswith(ROOT + os.sep)]
    inside = [e for e in inside if not os.path.realpath(e["file"]).startswith(os.path.join(ROOT, "build") + os.sep)]
    if not inside:
        sys.exit("no app sources in " + CDB)
    return inside


def gcc_command(entry):
    args = shlex.split(entry["command"])
    out = args.index("-o")
    args[out + 1] = os.devnull
    return [a for a in args if a != "-Werror"] + GCC_ANALYZER


def run_gcc(entry):
    r = subprocess.run(gcc_command(entry), cwd=entry["directory"], capture_output=True, text=True)
    text = r.stderr
    if r.returncode != 0 and not FINDING.search(text):
        text += "\n%s: error: compiler exited with %d\n" % (entry["file"], r.returncode)
    return text


def clang_database(entries):
    """Rewrite the GCC commands for clang-tidy (clang, ARM target, newlib)."""
    gcc = shlex.split(entries[0]["command"])[0]
    sysroot = subprocess.run([gcc, "-print-sysroot"], capture_output=True, text=True, check=True)
    newlib = os.path.join(sysroot.stdout.strip(), "include")
    if not os.path.isdir(newlib):
        sys.exit("newlib headers not found at " + newlib)
    db = []
    for e in entries:
        args = shlex.split(e["command"])
        out = args.index("-o")
        del args[out : out + 2]
        args = ["clang", "--target=arm-none-eabi"] + [a for a in args[1:] if a not in GCC_ONLY]
        args += ["-isystem", newlib, "-Wno-unknown-warning-option"]
        db.append({"directory": e["directory"], "file": e["file"], "arguments": args})
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(WORK, "compile_commands.json"), "w") as f:
        json.dump(db, f, indent=1)


def run_tidy(tidy, entry):
    header_filter = "^" + re.escape(ROOT + os.sep) + "(helpers|scenes|views|[^/]+\\.h$)"
    cmd = [tidy, "-p", WORK, "--quiet", "--header-filter=" + header_filter, entry["file"]]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    text = r.stdout + r.stderr
    if r.returncode != 0 and not FINDING.search(text):
        text += "\n%s: error: clang-tidy exited with %d\n" % (entry["file"], r.returncode)
    return text


def report(name, outputs):
    findings = set()
    for text in outputs:
        for m in FINDING.finditer(text):
            findings.add(m.group(0))
    shown = [t for t in outputs if FINDING.search(t)]
    for text in shown:
        print(text.rstrip())
    print("%s: %d finding(s)" % (name, len(findings)))
    return len(findings)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--skip-cdb", action="store_true", help="reuse .vscode/compile_commands.json")
    args = ap.parse_args()

    if not args.skip_cdb:
        subprocess.run(["ufbt", "cdb"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    entries = app_entries()
    tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy-18") or shutil.which("clang-tidy")
    if not tidy:
        sys.exit("clang-tidy not found (set CLANG_TIDY)")
    version = subprocess.run([tidy, "--version"], capture_output=True, text=True).stdout
    print("%d source files; %s" % (len(entries), " ".join(version.split())))

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        gcc_out = list(pool.map(run_gcc, entries))
        clang_database(entries)
        tidy_out = list(pool.map(lambda e: run_tidy(tidy, e), entries))

    total = report("gcc -fanalyzer", gcc_out) + report("clang-tidy", tidy_out)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
