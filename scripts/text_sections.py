#!/usr/bin/env python3
"""Which code section each of the app's source files goes to, and a check.

    scripts/text_sections.py --check            # CI: every function annotated
    scripts/text_sections.py --fix              # annotate what is missing
    scripts/text_sections.py --elf APP_d.elf    # the built app agrees

The resident app is cut into five code sections, each a group of whole
source files (radiogeddon_edition.h). A file's functions carry its group's
macro: RG_UI_TEXT (scenes/), RG_VIEW_TEXT (views/), RG_CORE_TEXT,
RG_LAB_TEXT or RG_TOOL_TEXT (the helpers listed below); every other file of
the app stays in .text. A function missing its macro would land in .text and make that
section grow back, so --check fails on it, and on a macro in a file of
another group. --elf reads the debug ELF of a build (arm-none-eabi-nm -l)
and checks every function's section against its file's group.

Standard library only (--elf needs arm-none-eabi-nm on PATH).
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Helpers outside .text, by section macro. Whole files only.
HELPER_GROUPS = {
    "RG_CORE_TEXT": (
        "helpers/radiogeddon_subghz.c",
        "helpers/radiogeddon_decoders.c",
        "helpers/radiogeddon_decode_text.c",
        "helpers/radiogeddon_analysis_decode.c",
        "helpers/rg_decode.c",
        "helpers/radiogeddon_history.c",
        "helpers/radiogeddon_recorder.c",
        "helpers/rg_ring.c",
        "helpers/rg_raw.c",
        "helpers/rg_rawfmt.c",
        "helpers/radiogeddon_scanner.c",
        "helpers/rg_scan.c",
        "helpers/rg_adapt.c",
        "helpers/radiogeddon_dsp.c",
        "helpers/rg_txpolicy.c",
        "helpers/rg_preset.c",
    ),
    # Not rg_ui.c nor rg_journal.c: half their functions (names and texts)
    # serve only modules, and a named section cannot drop unused functions
    # (the linker's gc works per section), so they stay in .text.
    "RG_LAB_TEXT": (
        "helpers/radiogeddon_ui.c",
        "helpers/radiogeddon_research.c",
        "helpers/rg_mascot.c",
        "helpers/rg_assist.c",
    ),
    # Resident only in the Catalog edition (module-only in the Full one).
    "RG_TOOL_TEXT": (
        "helpers/rg_analyzer.c",
        "helpers/radiogeddon_analysis.c",
        "helpers/radiogeddon_analysis_unknown.c",
        "helpers/radiogeddon_analysis_run.c",
        "helpers/radiogeddon_report.c",
        "helpers/rg_timeline.c",
        "helpers/radiogeddon_recovery.c",
        "helpers/rg_recover.c",
        "helpers/rg_checksum.c",
    ),
}
SECTION_OF = {
    "RG_UI_TEXT": ".rgtext",
    "RG_VIEW_TEXT": ".rgview",
    "RG_CORE_TEXT": ".rgcore",
    "RG_LAB_TEXT": ".rglab",
    "RG_TOOL_TEXT": ".rgtool",
}
MACROS = tuple(SECTION_OF)


def group_of(path):
    """The macro a file's functions carry, or None for .text."""
    if path.startswith("scenes/"):
        return "RG_UI_TEXT"
    if path.startswith("views/"):
        return "RG_VIEW_TEXT"
    for macro, files in HELPER_GROUPS.items():
        if path in files:
            return macro
    return None


def app_sources():
    out = ["radiogeddon.c"]
    for d in ("scenes", "views", "helpers"):
        for name in sorted(os.listdir(os.path.join(ROOT, d))):
            if name.endswith(".c"):
                out.append(d + "/" + name)
    return out


def blank_comments_and_strings(text):
    """The text with comments, string/char literals and preprocessor lines
    replaced by spaces (same length, newlines kept)."""
    out = list(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c in " \t":
            i += 1
            continue
        if line_start and c == "#":
            # Preprocessor line, with continuations.
            while i < n and text[i] != "\n":
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    out[i] = " "
                    i += 2
                    continue
                out[i] = " "
                i += 1
            continue
        line_start = c == "\n"
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if out[k] != "\n":
                    out[k] = " "
            i = end
            continue
        if c in "\"'":
            q = c
            out[i] = " "
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n and out[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
            i += 1
            continue
        i += 1
    return "".join(out)


DECL_SKIP = re.compile(r"^\s*(typedef|struct|union|enum)\b")


def function_definitions(text):
    """(start offset, header text) of every function definition at file
    scope: a statement at brace depth 0 that ends in '{' and has a
    parameter list, and is not a type or an initialised object."""
    clean = blank_comments_and_strings(text)
    defs = []
    depth = 0
    stmt_start = None
    i, n = 0, len(clean)
    while i < n:
        c = clean[i]
        if depth == 0:
            if stmt_start is None and not c.isspace() and c not in ";}":
                stmt_start = i
            if c == ";":
                stmt_start = None
            elif c == "{":
                header = clean[stmt_start:i] if stmt_start is not None else ""
                if (
                    stmt_start is not None
                    and "(" in header
                    and "=" not in header.split("(")[0]
                    and not DECL_SKIP.match(header)
                    and header.rstrip().endswith(")")
                ):
                    defs.append((stmt_start, header))
                stmt_start = None
                depth = 1
        else:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    stmt_start = None
        i += 1
    return defs


def macros_in(header):
    return [m for m in MACROS if re.search(r"\b%s\b" % m, header)]


def check_file(path, fix):
    full = os.path.join(ROOT, path)
    text = open(full, encoding="utf-8").read()
    want = group_of(path)
    problems = []
    inserts = []
    for start, header in function_definitions(text):
        name = re.search(r"(\w+)\s*\(", header)
        fname = name.group(1) if name else "?"
        have = macros_in(header)
        line = text.count("\n", 0, start) + 1
        if want is None:
            if have:
                problems.append("%s:%d %s: %s in a .text file" % (path, line, fname, have[0]))
        elif have != [want]:
            if not have:
                inserts.append(start)
            problems.append(
                "%s:%d %s: needs %s%s"
                % (path, line, fname, want, " (has %s)" % ",".join(have) if have else "")
            )
    if fix and inserts and want:
        for start in sorted(inserts, reverse=True):
            m = re.match(r"((?:static\s+|inline\s+)*)", text[start:])
            pos = start + len(m.group(1))
            text = text[:pos] + want + " " + text[pos:]
        if "radiogeddon_edition.h" not in text and want not in ("RG_UI_TEXT", "RG_VIEW_TEXT"):
            # The first include line gets the macros' header after it.
            m = re.search(r'^#include "[^"]+"\n', text, re.M)
            text = (
                text[: m.end()]
                + '#include "../radiogeddon_edition.h" /* %s */\n' % want
                + text[m.end() :]
            )
        open(full, "w", encoding="utf-8").write(text)
    return problems


def check_elf(elf):
    """Every function of the built app in its file's section."""
    nm = os.environ.get("NM", "arm-none-eabi-nm")
    readelf = nm[: -len("nm")] + "readelf"
    names = {}
    for line in subprocess.check_output([readelf, "-SW", elf], text=True).splitlines():
        m = re.match(r"\s*\[\s*(\d+)\]\s+(\S+)", line)
        if m:
            names[m.group(1)] = m.group(2)
    # (name, value) -> section name, for every function symbol.
    where_sym = {}
    for line in subprocess.check_output([readelf, "-sW", elf], text=True).splitlines():
        p = line.split()
        if len(p) >= 8 and p[3] == "FUNC":
            value = int(p[1], 16) & ~1
            where_sym[(p[7], value)] = names.get(p[6], "?")
    bad = []
    count = 0
    out = subprocess.check_output([nm, "-l", "--defined-only", elf], text=True)
    for line in out.splitlines():
        p = line.split()
        if len(p) < 4 or p[1].lower() != "t":
            continue
        addr, fname, src = int(p[0], 16) & ~1, p[2], re.sub(r":\d+$", "", p[3])
        rel = None
        for d in ("scenes/", "views/", "helpers/"):
            k = src.rfind("/" + d)
            if k >= 0:
                rel = src[k + 1 :]
        if rel is None and src.endswith("/radiogeddon.c"):
            rel = "radiogeddon.c"
        if rel is None or not rel.endswith(".c"):
            continue  # a header's static inline function: .text
        want = group_of(rel)
        want_sec = SECTION_OF[want] if want else ".text"
        if (fname, addr) not in where_sym:
            continue  # a local label (NOTYPE), not a function
        got = where_sym[(fname, addr)]
        count += 1
        if got != want_sec:
            bad.append("%s (%s): in %s, expected %s" % (fname, rel, got, want_sec))
    return count, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--fix", action="store_true")
    ap.add_argument("--elf")
    args = ap.parse_args()
    if not (args.check or args.fix or args.elf):
        ap.error("give --check, --fix or --elf")
    rc = 0
    if args.check or args.fix:
        problems = []
        for path in app_sources():
            problems += check_file(path, args.fix)
        for macro, files in HELPER_GROUPS.items():
            for f in files:
                if not os.path.exists(os.path.join(ROOT, f)):
                    problems.append("%s: listed for %s but missing" % (f, macro))
        if problems and not args.fix:
            print("\n".join(problems))
            print("%d function(s) not in their file's code section" % len(problems))
            rc = 1
        elif args.fix:
            print("annotated %d function(s)" % len(problems))
        else:
            print("every function of the app carries its file's section macro")
    if args.elf:
        count, bad = check_elf(args.elf)
        if bad:
            print("\n".join(bad))
            print("%d of %d functions in the wrong section" % (len(bad), count))
            rc = 1
        else:
            print("%d functions, each in its file's section" % count)
    return rc


if __name__ == "__main__":
    sys.exit(main())
