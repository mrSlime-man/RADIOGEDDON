#!/usr/bin/env python3
"""Check the Catalog edition against the Flipper Apps Catalog's rules.

    scripts/check_catalog.py [--catalog DIR] [--submission]
    scripts/check_catalog.py --write-manifest OUT --commit SHA

The rules come from the catalog's documentation/Manifest.md,
documentation/Contributing.md and AGENTS.md (flipperdevices/
flipper-application-catalog). Checked here, without building:

- application.fam: one external app, appid [a-z0-9_]+, a catalog category,
  name, author, fap_version "major.minor", fap_description, fap_icon, and the
  Catalog edition define;
- the icon: a 10x10 PNG with only black and white pixels;
- catalog/description.md and catalog/changelog.md: present, and using only
  the catalog's Markdown subset (with --catalog, the catalog's own filter from
  tools/flipp_catalog/markdown_filter.py; otherwise an equivalent check);
- screenshots in catalog/screenshots: each 512x256 (or 1024x512), landscape,
  only qFlipper's two colours, orange (254,138,44) and black;
- every string in the app's C sources is plain ASCII (the Flipper font has no
  other glyphs);
- the app writes only under /ext/apps_data/radiogeddon.

Missing screenshots are reported as a blocker; the exit status is 1 for them
only with --submission. Anything else wrong is always exit status 1.

--write-manifest writes the catalog's manifest.yml from
catalog/manifest.template.yml for commit SHA (40 hex digits), listing the
screenshots found. Standard library only (the --catalog filter needs the
`markdown` package, as the catalog's tools do).
"""
import argparse
import ast
import glob
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATEGORIES = ("Sub-GHz", "RFID", "NFC", "Infrared", "GPIO", "iButton", "USB", "Games", "Media", "Tools", "Bluetooth")
QFLIPPER_COLORS = {(254, 138, 44), (0, 0, 0)}
APP_DATA = "apps_data/radiogeddon"


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def fam_apps():
    """The App(...) calls of application.fam as dicts of literal arguments."""
    tree = ast.parse(read("application.fam"))
    apps = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and getattr(node.func, "id", "") == "App":
            args = {}
            for kw in node.keywords:
                try:
                    args[kw.arg] = ast.literal_eval(kw.value)
                except ValueError:
                    args[kw.arg] = None  # FlipperAppType.EXTERNAL and the like
            apps.append(args)
    return apps


def png_pixels(path):
    """(width, height, rgb rows) of a non-interlaced 8-bit or 1-bit PNG."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, chunks = 8, {}
    idat = b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IDAT":
            idat += body
        else:
            chunks.setdefault(kind, body)
        pos += 12 + length
    w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(">IIBBBBB", chunks[b"IHDR"])
    if interlace:
        raise ValueError("interlaced PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp_bits = depth * channels
    stride = (w * bpp_bits + 7) // 8
    raw = zlib.decompress(idat)
    bpp = max(1, bpp_bits // 8)
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        prev = line
        px = []
        for x in range(w):
            if depth < 8:
                bit = (line[(x * bpp_bits) // 8] >> (8 - depth - (x * bpp_bits) % 8)) & ((1 << depth) - 1)
                v = bit * 255 // ((1 << depth) - 1)
                vals = [v]
            else:
                vals = list(line[x * channels : (x + 1) * channels])
            if ctype == 3:
                pal = chunks[b"PLTE"]
                idx = vals[0]
                px.append(tuple(pal[idx * 3 : idx * 3 + 3]))
            elif channels in (1, 2):
                px.append((vals[0],) * 3)
            else:
                px.append(tuple(vals[:3]))
        rows.append(px)
    return w, h, rows


FORBIDDEN_MD = (
    (re.compile(r"^#{3,}\s", re.M), "header deeper than level 2"),
    (re.compile(r"^.+\n(=+|-+)\s*$", re.M), "setext header"),
    (re.compile(r"`"), "inline code or code block"),
    (re.compile(r"!\["), "image"),
    (re.compile(r"<[A-Za-z/!]"), "raw HTML"),
    (re.compile(r"&[A-Za-z#0-9]+;"), "HTML entity"),
    (re.compile(r"^\s*>", re.M), "blockquote"),
    (re.compile(r"^\s*([-*_])(\s*\1){2,}\s*$", re.M), "horizontal rule"),
    (re.compile(r"^\s*\|.*\|\s*$", re.M), "table"),
    (re.compile(r"^\s*\[[^\]]+\]:\s", re.M), "reference link definition"),
    (re.compile(r"^( {4}|\t)\S", re.M), "indented code block"),
)


def markdown_problems(rel, text, catalog_dir):
    if catalog_dir:
        sys.path.insert(0, os.path.join(catalog_dir, "tools"))
        from markdown import Markdown  # noqa: E402  (the catalog's dependency)
        from flipp_catalog.markdown_filter import BasicFormattingEnforcingExtension  # noqa: E402

        try:
            Markdown(extensions=[BasicFormattingEnforcingExtension()]).convert(text)
        except Exception as exc:  # the catalog's filter raises plain Exception
            return ["%s: %s (catalog filter)" % (rel, exc)]
        return []
    return ["%s: %s not allowed in the catalog" % (rel, why) for rx, why in FORBIDDEN_MD if rx.search(text)]


C_STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def source_problems():
    problems = []
    files = ["radiogeddon.c", "radiogeddon.h"]
    for d in ("scenes", "views", "helpers"):
        files += sorted(os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, d, "*.[ch]")))
    for rel in files:
        text = read(rel)
        # Strip comments, which may use any characters.
        code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        code = re.sub(r"//[^\n]*", "", code)
        for m in C_STRING.finditer(code):
            if any(ord(ch) > 127 for ch in m.group(0)):
                problems.append("%s: non-ASCII string %s" % (rel, m.group(0)[:40]))
        for m in re.finditer(r'EXT_PATH\("([^"]*)"\)', code):
            path = m.group(1)
            if path not in ("apps_data", APP_DATA) and not path.startswith(APP_DATA + "/"):
                problems.append("%s: writes outside %s: %s" % (rel, APP_DATA, path))
        for m in re.finditer(r'"/ext/([^"]*)"', code):
            if not m.group(1).startswith(APP_DATA):
                problems.append("%s: path outside %s: /ext/%s" % (rel, APP_DATA, m.group(1)))
    return problems


def screenshot_list():
    return sorted(
        os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, "catalog", "screenshots", "*.png"))
    )


def check(catalog_dir, submission):
    problems, blockers = [], []
    apps = [a for a in fam_apps()]
    if len(apps) != 1:
        problems.append("application.fam: expected one App(), found %d" % len(apps))
        return problems, blockers
    app = apps[0]
    if not re.match(r"^[a-z0-9_]+$", app.get("appid") or ""):
        problems.append("application.fam: appid %r does not match [a-z0-9_]+" % app.get("appid"))
    if app.get("fap_category") not in CATEGORIES:
        problems.append("application.fam: fap_category %r is not a catalog category" % app.get("fap_category"))
    if not re.match(r"^\d+\.\d+$", app.get("fap_version") or ""):
        problems.append("application.fam: fap_version %r is not major.minor" % app.get("fap_version"))
    for key in ("name", "fap_author", "fap_description", "fap_icon"):
        if not app.get(key):
            problems.append("application.fam: %s missing" % key)
    if "RADIOGEDDON_EDITION_CATALOG" not in (app.get("cdefines") or []):
        problems.append("application.fam: does not build the Catalog edition")

    icon = app.get("fap_icon")
    if icon:
        try:
            w, h, rows = png_pixels(os.path.join(ROOT, icon))
            colors = {px for row in rows for px in row}
            if (w, h) != (10, 10):
                problems.append("%s: %dx%d, the catalog needs 10x10" % (icon, w, h))
            if not colors <= {(0, 0, 0), (255, 255, 255)}:
                problems.append("%s: not black and white only (%d colours)" % (icon, len(colors)))
        except (OSError, ValueError, KeyError) as exc:
            problems.append("%s: %s" % (icon, exc))

    for rel in ("catalog/description.md", "catalog/changelog.md"):
        if not os.path.isfile(os.path.join(ROOT, rel)):
            problems.append("%s: missing" % rel)
            continue
        text = read(rel)
        if not text.strip():
            problems.append("%s: empty" % rel)
        problems += markdown_problems(rel, text, catalog_dir)
        if any(ord(ch) > 127 for ch in text):
            problems.append("%s: non-ASCII characters" % rel)
    if not re.search(r"^v%s:" % re.escape(app.get("fap_version") or "?"), read("catalog/changelog.md"), re.M):
        problems.append("catalog/changelog.md: no 'v%s:' entry for the current version" % app.get("fap_version"))

    shots = screenshot_list()
    if not shots:
        blockers.append(
            "catalog/screenshots: no screenshots; the catalog needs at least one genuine "
            "qFlipper screenshot from a Flipper Zero running the Catalog edition"
        )
    for rel in shots:
        try:
            w, h, rows = png_pixels(os.path.join(ROOT, rel))
        except (OSError, ValueError, KeyError) as exc:
            problems.append("%s: %s" % (rel, exc))
            continue
        if (w, h) not in ((512, 256), (1024, 512)):
            problems.append("%s: %dx%d, qFlipper saves 512x256" % (rel, w, h))
        colors = {px for row in rows for px in row}
        if not colors <= QFLIPPER_COLORS or len(colors) != 2:
            problems.append("%s: colours %s are not qFlipper's orange and black" % (rel, sorted(colors)[:4]))

    problems += source_problems()
    if submission:
        problems += blockers
    return problems, blockers


def write_manifest(out, commit):
    if not re.match(r"^[0-9a-f]{40}$", commit):
        sys.exit("--commit must be a full 40-character commit SHA")
    shots = screenshot_list()
    if not shots:
        sys.exit("no screenshots in catalog/screenshots: the catalog refuses a manifest without them")
    text = read("catalog/manifest.template.yml")
    text = text.replace('"@COMMIT@"', commit)
    text = text.replace('"@SCREENSHOTS@"', "\n" + "".join("  - %s\n" % s for s in shots).rstrip("\n"))
    text = "\n".join(line for line in text.splitlines() if not line.startswith("#")).lstrip("\n") + "\n"
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s for %s with %d screenshot(s)" % (out, commit, len(shots)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--catalog", help="flipper-application-catalog checkout: use its Markdown filter")
    ap.add_argument("--submission", action="store_true", help="missing screenshots are an error")
    ap.add_argument("--write-manifest", metavar="OUT", help="write the catalog manifest.yml")
    ap.add_argument("--commit", help="commit SHA for --write-manifest")
    args = ap.parse_args()
    if args.write_manifest:
        write_manifest(args.write_manifest, args.commit or "")
        return 0
    problems, blockers = check(args.catalog, args.submission)
    for p in problems:
        print("error: " + p)
    for b in blockers:
        if not args.submission:
            print("blocker: " + b)
    if problems:
        return 1
    print("catalog checks OK%s" % (" (except the blockers above)" if blockers else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
