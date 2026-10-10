#!/usr/bin/env python3
"""Write a buildable copy of RadioGeddon for one edition.

    scripts/stage_edition.py <full|catalog> DEST

Copies every file git tracks (the working-tree version, so local edits are
built) into DEST, then rewrites DEST/application.fam for the edition:

- catalog: application.fam as committed (it already is the Catalog edition);
- full:    appid "radiogeddon_full", name "RadioGeddon Full", the Full
           description and cdefines ["RADIOGEDDON_EDITION_FULL"].

Each rewrite must match exactly once, so a reworded application.fam fails the
build instead of silently producing the wrong edition. Both editions keep
their data in /ext/apps_data/radiogeddon (radiogeddon_storage.h), so captures
and settings carry over when switching editions.

Standard library only.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FULL_REWRITES = (
    (r'appid="radiogeddon"', 'appid="radiogeddon_full"'),
    (r'name="RadioGeddon"', 'name="RadioGeddon Full"'),
    (r'cdefines=\["RADIOGEDDON_EDITION_CATALOG"\]', 'cdefines=["RADIOGEDDON_EDITION_FULL"]'),
    (
        r'fap_description="[^"]*"',
        'fap_description="Full edition: Sub-GHz research toolkit: range scanning, waterfall, '
        'bitstream explorer, multi-capture compare, sessions, recording, decoding and replay."',
    ),
)

# appid and name of each edition, as written into the .fap manifest.
EDITIONS = {
    "catalog": {"appid": "radiogeddon", "name": "RadioGeddon"},
    "full": {"appid": "radiogeddon_full", "name": "RadioGeddon Full"},
}


def rewrite_fam(text, edition):
    if edition == "catalog":
        if '"RADIOGEDDON_EDITION_CATALOG"' not in text:
            raise SystemExit("application.fam does not define RADIOGEDDON_EDITION_CATALOG")
        return text
    for pattern, replacement in FULL_REWRITES:
        text, n = re.subn(pattern, replacement, text)
        if n != 1:
            raise SystemExit("application.fam: expected one match for %s, found %d" % (pattern, n))
    return text


def tracked_files():
    out = subprocess.check_output(["git", "-C", ROOT, "ls-files", "-z"])
    return [p for p in out.decode("utf-8").split("\0") if p]


def main(argv):
    if len(argv) != 3 or argv[1] not in EDITIONS:
        print("usage: %s <%s> DEST" % (argv[0], "|".join(EDITIONS)), file=sys.stderr)
        return 64
    edition, dest = argv[1], os.path.abspath(argv[2])
    rel = os.path.relpath(dest, ROOT)
    if rel == "." or (not rel.startswith("..") and not rel.split(os.sep)[0].startswith(".")):
        # A destination inside the tree would be compiled by the next build
        # (ufbt globs the sources); dot-directories such as .ufbt-* are skipped.
        raise SystemExit("DEST must be outside the source tree or in a dot-directory")
    if os.path.exists(dest):
        shutil.rmtree(dest)
    for rel in tracked_files():
        src = os.path.join(ROOT, rel)
        if not os.path.isfile(src):
            continue  # deleted in the working tree
        dst = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    fam = os.path.join(dest, "application.fam")
    with open(fam, encoding="utf-8") as f:
        text = rewrite_fam(f.read(), edition)
    with open(fam, "w", encoding="utf-8") as f:
        f.write(text)
    print("staged %s edition (appid %s) in %s" % (edition, EDITIONS[edition]["appid"], dest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
