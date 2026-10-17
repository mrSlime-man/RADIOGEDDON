#!/usr/bin/env python3
"""Check the app's calls against the firmware's furi_check() argument contracts.

Many firmware API functions stop the device ("furi_check failed", then a
reboot) when an argument is NULL: variable_item_list_set_enter_callback()
checks its callback, text_box_set_text() its text, notification_message()
its sequence, and so on. Host tests cannot see this (they use stubs), and
the beta 11 Adaptive Scanner crash on RogueMaster was exactly such a call.

The contracts live in scripts/fw_contracts.json: for each function, the
(zero-based) arguments the firmware furi_check()s. They are derived from a
firmware source tree with --derive; the committed table is the union over
the pinned RogueMaster, Official, Momentum and Unleashed trees.

    scripts/check_fw_contracts.py                 # check the app's sources
    scripts/check_fw_contracts.py --derive FWDIR  # print a table from FWDIR

A call is reported when a checked argument is the literal NULL (or 0 cast to
a pointer). Exit status 1 if any call breaks a contract.
"""

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TABLE = ROOT / "scripts" / "fw_contracts.json"
APP_DIRS = ["radiogeddon.c", "scenes", "helpers", "views", "modules"]

# Firmware areas an external app calls into.
DERIVE_GLOBS = [
    "applications/services/gui/**/*.c",
    "applications/services/storage/*.c",
    "applications/services/notification/*.c",
    "applications/services/dialogs/*.c",
    "applications/services/loader/*.c",
    "furi/core/*.c",
    "lib/subghz/**/*.c",
    "lib/flipper_format/*.c",
    "lib/flipper_application/**/*.c",
    "lib/toolbox/**/*.c",
]

FUNC_RE = re.compile(
    r"\n(?P<head>[A-Za-z_][\w \*]*?\b(?P<name>\w+)\s*\((?P<params>[^)]*)\)\s*\{)(?P<body>.*?)\n\}",
    re.S,
)


def derive(fw_dir):
    """{function: [checked argument indices]} for a firmware tree."""
    table = {}
    for pattern in DERIVE_GLOBS:
        for path in sorted(pathlib.Path(fw_dir).glob(pattern)):
            src = path.read_text(errors="ignore")
            for m in FUNC_RE.finditer(src):
                if "static" in m.group("head").split("(")[0]:
                    continue
                params = [p.strip() for p in m.group("params").split(",")]
                if not params or params == ["void"] or params == [""]:
                    continue
                names = [re.split(r"[\s\*]+", p)[-1] for p in params]
                # Only the checks at the top of the body: a function's own
                # preconditions, not checks deep inside its logic.
                head = m.group("body")[:600]
                checked = set()
                for c in re.findall(r"furi_check\(\s*(\w+)\s*[,)]", head):
                    if c in names:
                        checked.add(names.index(c))
                if checked:
                    table[m.group("name")] = sorted(checked)
    return table


def split_args(text, start):
    """The arguments of the call whose '(' is at text[start]; None if unbalanced."""
    depth = 0
    args = []
    cur = []
    i = start
    while i < len(text):
        ch = text[i]
        if ch in "([{":
            depth += 1
            if depth > 1:
                cur.append(ch)
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args
            cur.append(ch)
        elif ch == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
        elif ch == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i : j + 1])
            i = j
        else:
            cur.append(ch)
        i += 1
    return None


NULLISH = re.compile(r"^\(?\s*(\(\s*[\w\s\*]+\)\s*)?(NULL|0|nullptr)\s*\)?$")


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def app_files():
    files = []
    for entry in APP_DIRS:
        p = ROOT / entry
        files += [p] if p.is_file() else sorted(p.rglob("*.c")) + sorted(p.rglob("*.h"))
    return files


def check(table, files):
    call_re = re.compile(r"\b(" + "|".join(map(re.escape, sorted(table))) + r")\s*\(")
    problems = []
    for path in files:
        src = strip_comments(path.read_text(errors="ignore"))
        for m in call_re.finditer(src):
            # Skip the declaration/definition of a same-named app function.
            before = src[max(0, m.start() - 40) : m.start()]
            if re.search(r"(void|bool|\w+\*)\s*$", before):
                continue
            args = split_args(src, m.end() - 1)
            if args is None:
                continue
            for idx in table[m.group(1)]:
                if idx < len(args) and NULLISH.match(args[idx]):
                    line = src.count("\n", 0, m.start()) + 1
                    try:
                        shown = path.relative_to(ROOT)
                    except ValueError:
                        shown = path
                    problems.append(
                        f"{shown}:{line}: {m.group(1)}() argument {idx + 1} "
                        f"is NULL, but the firmware furi_check()s it (device crash)"
                    )
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--derive", metavar="FWDIR", help="print the contract table of a firmware tree")
    ap.add_argument("--merge", action="store_true", help="with --derive: merge into the committed table")
    ap.add_argument("--self-test", action="store_true", help="prove the check finds the beta 11 crash")
    args = ap.parse_args()
    if args.self_test:
        import tempfile

        table = json.loads(TABLE.read_text())["contracts"]
        bad = (
            "void exit(void* context) {\n"
            "    App* app = context;\n"
            "    variable_item_list_set_enter_callback(app->list, NULL, NULL);\n"
            "    variable_item_list_set_enter_callback(app->list, cb, NULL); /* fine */\n"
            "    text_box_set_text(app->box, (const char*)0);\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory() as d:
            f = pathlib.Path(d) / "case.c"
            f.write_text(bad)
            found = check(table, [f])
        ok = len(found) == 2 and ":3:" in found[0] and ":5:" in found[1]
        print("self-test", "passed" if ok else "FAILED", *found, sep="\n")
        return 0 if ok else 1
    if args.derive:
        table = derive(args.derive)
        if args.merge:
            old = json.loads(TABLE.read_text())["contracts"] if TABLE.exists() else {}
            for k, v in table.items():
                old[k] = sorted(set(old.get(k, [])) | set(v))
            table = old
        doc = {
            "about": "Firmware functions and the zero-based arguments they furi_check() "
            "(scripts/check_fw_contracts.py --derive).",
            "contracts": dict(sorted(table.items())),
        }
        out = json.dumps(doc, indent=1) + "\n"
        if args.merge:
            TABLE.write_text(out)
        else:
            sys.stdout.write(out)
        return 0
    table = json.loads(TABLE.read_text())["contracts"]
    problems = check(table, app_files())
    for p in problems:
        print(p)
    print(f"{len(table)} firmware contracts, {len(problems)} violation(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
