#!/usr/bin/env python3
"""Check that the version, release notes and download links agree.

    scripts/release_meta.py                     # CI, on every pull request
    scripts/release_meta.py --tag v1.0.0-beta.3 [--github-output FILE]

The version in radiogeddon_version.h names the release the tree is (or is
about to become). For that version this checks:

- it is MAJOR.MINOR.PATCH[-prerelease] and application.fam's fap_version is
  MAJOR.MINOR;
- docs/releases/v<version>.md exists and its first line is the release title;
- CHANGELOG.md has an [Unreleased] section and a [<version>] section;
- every API version the release notes and docs/FIRMWARE_COMPATIBILITY.md
  give for a .fap equals the one pinned in scripts/firmware_pins.sh, so the
  notes never describe a build the release does not contain;
- every release link in README.md and docs/INSTALLATION.md points at
  v<version>, so the download links move with the version.

With --tag it also requires the tag to be v<version>, and writes tag, title
and prerelease to --github-output for the Release workflow. Exits 1 with one
line per problem.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ("official", "unleashed", "roguemaster")
VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(-[0-9A-Za-z.]+)?$")
# A table cell naming `radiogeddon-<target>.fap` next to a cell with its API,
# in either order ("| `radiogeddon-official.fap` | 87.1 |", "| **87.1** | `...` |").
FAP_THEN_API = re.compile(r"`radiogeddon-(\w+)\.fap`[^|\n]*\|\s*\**(\d+\.\d+)\**\s*\|")
API_THEN_FAP = re.compile(r"\|\s*\**(\d+\.\d+)\**\s*\|\s*`radiogeddon-(\w+)\.fap`")
RELEASE_LINK = re.compile(r"/releases/(?:tag|download)/(v[^/)\s]+)")


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def pinned_apis():
    pins = dict(re.findall(r'^([A-Z_]+)="([^"]*)"', read("scripts/firmware_pins.sh"), re.M))
    return {t: pins.get(t.upper() + "_API") for t in TARGETS}


def fap_api_problems(rel, text, apis):
    """Every table row giving a .fap's API must match the pins, and each
    target must appear at least once."""
    problems = []
    found = {}
    rows = [m.groups() for m in FAP_THEN_API.finditer(text)]
    rows += [(m.group(2), m.group(1)) for m in API_THEN_FAP.finditer(text)]
    for target, api in rows:
        found[target] = api
        if target not in apis:
            problems.append("%s: unknown artifact radiogeddon-%s.fap" % (rel, target))
        elif api != apis[target]:
            problems.append(
                "%s: radiogeddon-%s.fap is listed with API %s, pinned API is %s" % (rel, target, api, apis[target])
            )
    for target in TARGETS:
        if target not in found:
            problems.append("%s: no API listed for radiogeddon-%s.fap" % (rel, target))
    return problems


def check(tag=None):
    problems = []
    version = re.search(r'^#define RADIOGEDDON_VERSION "(.*)"$', read("radiogeddon_version.h"), re.M)
    version = version.group(1) if version else ""
    m = VERSION.match(version)
    if not m:
        return ["radiogeddon_version.h: '%s' is not MAJOR.MINOR.PATCH[-prerelease]" % version], None
    fap = re.search(r'^\s*fap_version="([0-9.]*)"', read("application.fam"), re.M)
    if not fap or fap.group(1) != "%s.%s" % (m.group(1), m.group(2)):
        problems.append(
            "application.fam: fap_version %s, expected %s.%s from version %s"
            % (fap.group(1) if fap else "missing", m.group(1), m.group(2), version)
        )
    if tag is not None and tag != "v" + version:
        problems.append("tag %s does not match radiogeddon_version.h (%s)" % (tag, version))

    notes_rel = "docs/releases/v%s.md" % version
    title = ""
    if not os.path.isfile(os.path.join(ROOT, notes_rel)):
        problems.append("%s: missing (release notes for the version in radiogeddon_version.h)" % notes_rel)
        notes = ""
    else:
        notes = read(notes_rel)
        title = re.sub(r"^#\s*", "", notes.split("\n", 1)[0]).strip()
        if not title:
            problems.append("%s: first line must be the release title" % notes_rel)

    changelog = read("CHANGELOG.md")
    if not re.search(r"^## \[Unreleased\]", changelog, re.M):
        problems.append("CHANGELOG.md: no [Unreleased] section")
    if not re.search(r"^## \[%s\]" % re.escape(version), changelog, re.M):
        problems.append("CHANGELOG.md: no [%s] section" % version)

    apis = pinned_apis()
    if notes:
        problems += fap_api_problems(notes_rel, notes, apis)
    problems += fap_api_problems("docs/FIRMWARE_COMPATIBILITY.md", read("docs/FIRMWARE_COMPATIBILITY.md"), apis)

    for rel in ("README.md", "docs/INSTALLATION.md"):
        links = RELEASE_LINK.findall(read(rel))
        if not links:
            problems.append("%s: no release download links" % rel)
        for linked in sorted(set(links)):
            if linked != "v" + version:
                problems.append("%s: links to release %s, the version is v%s" % (rel, linked, version))

    meta = {"tag": "v" + version, "title": title, "prerelease": "true" if m.group(4) else "false"}
    return problems, meta


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", help="release tag to validate (vMAJOR.MINOR.PATCH[-prerelease])")
    ap.add_argument("--github-output", help="append tag=, title= and prerelease= lines to this file")
    args = ap.parse_args()

    problems, meta = check(args.tag)
    for p in problems:
        print("error: " + p)
    if problems:
        return 1
    print("release metadata OK: %s (%s), prerelease=%s" % (meta["tag"], meta["title"], meta["prerelease"]))
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as f:
            for key in ("tag", "title", "prerelease"):
                f.write("%s=%s\n" % (key, meta[key]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
