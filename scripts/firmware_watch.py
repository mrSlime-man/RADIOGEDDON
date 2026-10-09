#!/usr/bin/env python3
"""Compare the pinned firmware SDKs with the newest firmware releases.

    scripts/firmware_watch.py [--build] [--work DIR]

Releases are built only against the SDKs pinned in scripts/firmware_pins.sh.
This looks up what each firmware family has published since:

- Official: the release and release-candidate channels of
  update.flipperzero.one (version, SDK, SHA-256 and API version);
- Unleashed: the newest unlshd-NNN tag and its API version;
- RogueMaster: the newest commit on its default branch and its API version.

With --build it also compiles the app against every Official or Unleashed
SDK that differs from the pin (a canary build: SDK checksum verified where the
firmware publishes one, the SDK's own API compatibility check, and the .fap
manifest checked with verify_fap.py). The checkout is not touched; the build
runs on a copy in --work. RogueMaster is only reported, since building it
needs its whole firmware tree.

Prints a Markdown table (also appended to $GITHUB_STEP_SUMMARY when set) and
exits 1 only if a canary build fails, which means a firmware the app will have
to support no longer builds it. A newer firmware alone is reported as a
warning: moving the pins needs the hardware checklist on that firmware.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OFFICIAL_DIRECTORY = "https://update.flipperzero.one/firmware/directory.json"
OFFICIAL_REPO = "https://github.com/flipperdevices/flipperzero-firmware"
UNLEASHED_REPO = "https://github.com/DarkFlippers/unleashed-firmware"
UNLEASHED_SDK = "https://unleashedflip.com/fw/{tag}/flipper-z-f7-sdk-{tag}.zip"
RAW = "https://raw.githubusercontent.com/{repo}/{ref}/targets/f7/api_symbols.csv"


def pins():
    with open(os.path.join(ROOT, "scripts", "firmware_pins.sh"), encoding="utf-8") as f:
        return dict(re.findall(r'^([A-Z_]+)="([^"]*)"', f.read(), re.M))


def fetch(url):
    return subprocess.run(
        ["curl", "-fsSL", "--retry", "3", "--retry-delay", "5", url], capture_output=True, check=True
    ).stdout


def api_of_csv(text):
    m = re.search(r"^Version,[^,]*,(\d+\.\d+),", text, re.M)
    return m.group(1) if m else "?"


def api_at(repo_url, ref):
    repo = repo_url.split("github.com/", 1)[1]
    try:
        return api_of_csv(fetch(RAW.format(repo=repo, ref=ref)).decode())
    except subprocess.CalledProcessError:
        return "?"


def ls_remote(url, *refs):
    out = subprocess.run(["git", "ls-remote", url, *refs], capture_output=True, text=True, check=True).stdout
    return [line.split("\t") for line in out.splitlines() if "\t" in line]


def official_channels():
    directory = json.loads(fetch(OFFICIAL_DIRECTORY))
    found = {}
    for channel in directory["channels"]:
        if channel["id"] not in ("release", "release-candidate") or not channel["versions"]:
            continue
        v = channel["versions"][0]
        sdk = [f for f in v["files"] if f.get("target") == "f7" and f.get("type") == "sdk_zip"]
        if sdk:
            found[channel["id"]] = (v["version"], sdk[0]["url"], sdk[0].get("sha256"))
    return found


def canary_build(name, url, sha256, work):
    """Build a copy of the app against the SDK at url; returns (ok, api, note)."""
    home = os.path.join(work, name)
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    zip_path = os.path.join(home, os.path.basename(url))
    with open(zip_path, "wb") as f:
        f.write(fetch(url))
    with open(zip_path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    if sha256 and digest != sha256:
        return False, "?", "SDK checksum mismatch"

    src = os.path.join(home, "src")
    os.makedirs(src)
    files = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    for rel in filter(None, files.decode().split("\0")):
        os.makedirs(os.path.dirname(os.path.join(src, rel)), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, rel), os.path.join(src, rel))

    env = dict(os.environ, UFBT_HOME=os.path.join(home, "ufbt"))
    env.setdefault("FBT_TOOLCHAIN_PATH", os.path.join(work, "toolchain"))
    log = os.path.join(home, "build.log")
    failed = None
    with open(log, "w") as out:
        for cmd in (["ufbt", "update", "--hw-target", "f7", "--local", zip_path], ["ufbt"]):
            if subprocess.run(cmd, cwd=src, env=env, stdout=out, stderr=subprocess.STDOUT).returncode != 0:
                failed = cmd
                break
    if failed:
        with open(log) as f:
            print("".join(f.readlines()[-40:]))
        return False, "?", "`%s` failed (log above)" % " ".join(failed[:2])
    with open(os.path.join(env["UFBT_HOME"], "current", "sdk_headers", "f7_sdk", "targets", "f7", "api_symbols.csv")) as f:
        api = api_of_csv(f.read())
    fap = os.path.join(src, "dist", "radiogeddon.fap")
    verify = subprocess.run(
        [sys.executable, os.path.join(ROOT, "scripts", "verify_fap.py"), fap, "--api", api, "--name", "RadioGeddon"],
        capture_output=True,
        text=True,
    )
    if verify.returncode != 0:
        return False, api, "manifest check failed: " + verify.stdout.strip().splitlines()[-1]
    return True, api, "builds (sha256 %s)" % digest[:12]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", action="store_true", help="canary-build against every SDK that differs from the pin")
    ap.add_argument("--work", help="build directory (default: a temporary one)")
    args = ap.parse_args()

    p = pins()
    pinned_official = re.search(r"/firmware/([^/]+)/", p["OFFICIAL_SDK_URL"]).group(1)
    pinned_unleashed = re.search(r"/fw/([^/]+)/", p["UNLEASHED_SDK_URL"]).group(1)
    rows, canaries, warnings = [], [], []

    official = official_channels()
    for channel, label in (("release", "Official release"), ("release-candidate", "Official release candidate")):
        if channel not in official:
            rows.append([label, pinned_official, "not published", "", ""])
            continue
        version, url, sha = official[channel]
        if version == pinned_official:
            rows.append([label, "%s (%s)" % (pinned_official, p["OFFICIAL_API"]), version, p["OFFICIAL_API"], "pinned"])
            continue
        rows.append([label, "%s (%s)" % (pinned_official, p["OFFICIAL_API"]), version, api_at(OFFICIAL_REPO, version), ""])
        canaries.append((len(rows) - 1, "official-" + channel, url, sha))
        if channel == "release":
            warnings.append("Official %s is out; the pin is %s" % (version, pinned_official))

    tags = [ref.rsplit("/", 1)[1] for _, ref in ls_remote(UNLEASHED_REPO, "refs/tags/unlshd-*")]
    tags = sorted({t for t in tags if re.fullmatch(r"unlshd-\d+", t)}, key=lambda t: int(t.split("-")[1]))
    latest = tags[-1] if tags else "?"
    rows.append(["Unleashed", "%s (%s)" % (pinned_unleashed, p["UNLEASHED_API"]), latest, "", ""])
    if latest == pinned_unleashed:
        rows[-1][3:] = [p["UNLEASHED_API"], "pinned"]
    elif tags:
        rows[-1][3] = api_at(UNLEASHED_REPO, latest)
        canaries.append((len(rows) - 1, "unleashed-" + latest, UNLEASHED_SDK.format(tag=latest), None))
        warnings.append("Unleashed %s is out; the pin is %s" % (latest, pinned_unleashed))

    head = ls_remote(p["ROGUEMASTER_REPO"], "HEAD")[0][0]
    pinned_rm = p["ROGUEMASTER_REF"]
    rows.append(["RogueMaster", "%s (%s)" % (pinned_rm[:7], p["ROGUEMASTER_API"]), head[:7], "", ""])
    if head == pinned_rm:
        rows[-1][3:] = [p["ROGUEMASTER_API"], "pinned"]
    else:
        rows[-1][3:] = [api_at(p["ROGUEMASTER_REPO"], head), "not built (needs the firmware tree)"]
        warnings.append("RogueMaster has moved to %s; the pin is %s" % (head[:7], pinned_rm[:7]))

    failed = False
    if canaries and args.build:
        work = args.work or tempfile.mkdtemp(prefix="firmware-watch-")
        for row, name, url, sha in canaries:
            print("canary build: %s (%s)" % (name, url), flush=True)
            ok, api, note = canary_build(name, url, sha, work)
            rows[row][3] = rows[row][3] or api
            rows[row][4] = ("OK: " if ok else "FAILED: ") + note
            failed |= not ok
    else:
        for row, *_ in canaries:
            rows[row][4] = "newer than the pin (run with --build)"

    table = ["| Firmware | Pinned (API) | Newest | Newest API | Canary build |", "|---|---|---|---|---|"]
    table += ["| " + " | ".join(r) + " |" for r in rows]
    text = "\n".join(table) + "\n"
    print(text)
    for w in warnings:
        print("::warning::" + w)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write("### Firmware watch\n\n" + text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
