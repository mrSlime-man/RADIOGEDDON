#!/usr/bin/env python3
"""Run the Flipper Apps Catalog's own bundler on the Catalog edition.

    scripts/catalog_bundle.py CATALOG_DIR COMMIT [--origin URL] [--out ZIP]

CATALOG_DIR is a checkout of flipperdevices/flipper-application-catalog with
its tools' requirements installed (pip install -r tools/requirements.txt) and
ufbt on PATH. COMMIT is the full SHA of a pushed RadioGeddon commit.

This writes the catalog manifest for COMMIT (scripts/check_catalog.py
--write-manifest) under CATALOG_DIR/applications/Sub-GHz/radiogeddon/ and
runs tools/bundle.py's AppBundler on it, step by step, exactly as the
catalog's validation does: clone the source at COMMIT, `ufbt lint`, `ufbt`
build, read the manifest from application.fam, check the manifest path,
load the description and changelog, process the screenshots and the icon,
check the values and Markdown, build the package.

Screenshots must come from a real Flipper Zero (qFlipper). Until they exist
the manifest cannot be written, so this runs every step that does not need
them on a manifest without screenshots and reports that step as the one left
(exit status 2). Any other failure is exit status 1.
"""
import argparse
import logging
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIGIN = "https://github.com/mrSlime-man/RADIOGEDDON.git"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("catalog")
    ap.add_argument("commit")
    ap.add_argument("--origin", default=ORIGIN)
    ap.add_argument("--out", default=os.path.join(tempfile.gettempdir(), "radiogeddon-bundle.zip"))
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    app_dir = os.path.join(args.catalog, "applications", "Sub-GHz", "radiogeddon")
    os.makedirs(app_dir, exist_ok=True)
    manifest = os.path.join(app_dir, "manifest.yml")
    have_shots = subprocess.run(
        [sys.executable, os.path.join(ROOT, "scripts", "check_catalog.py"), "--write-manifest", manifest, "--commit", args.commit],
        capture_output=True,
        text=True,
    )
    if have_shots.returncode != 0:
        # No screenshots yet: a manifest for everything else.
        with open(os.path.join(ROOT, "catalog", "manifest.template.yml"), encoding="utf-8") as f:
            text = f.read()
        text = text.replace('"@COMMIT@"', args.commit).replace('screenshots: "@SCREENSHOTS@"\n', "screenshots: []\n")
        with open(manifest, "w", encoding="utf-8") as f:
            f.write(text)
        print("no screenshots: bundling without them (" + have_shots.stdout.strip() + have_shots.stderr.strip() + ")")
    if args.origin != ORIGIN:
        with open(manifest, encoding="utf-8") as f:
            text = f.read().replace(ORIGIN, args.origin)
        with open(manifest, "w", encoding="utf-8") as f:
            f.write(text)

    sys.path.insert(0, os.path.join(args.catalog, "tools"))
    import bundle  # the catalog's own tools/bundle.py

    bundle.Main()._setup_imports()
    steps = (
        "_fetch_sources",
        "_lint_sources",
        "_build_sources",
        "_update_manifest_from_fap",
        "_check_manifest_path",
        "_process_includes",
        "_process_assets",
        "_check_manifest_values",
    )
    with bundle.AppBundler(manifest, args.out) as bundler:
        for step in steps:
            print("== %s" % step)
            try:
                getattr(bundler, step)()
            except bundle.BundlerException as exc:
                if step == "_process_assets" and "No screenshots" in str(exc):
                    print("   left: %s (needs qFlipper screenshots from hardware)" % exc)
                    # The values and Markdown do not depend on the screenshots.
                    bundler._check_manifest_values()
                    print("== _check_manifest_values: OK")
                    return 2
                print("FAILED at %s: %s" % (step, exc))
                return 1
            print("   OK")
        bundler._build_package()
    print("bundle written to %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
