#!/usr/bin/env python3
"""Check links in the repository's Markdown files.

Verifies, offline and deterministically, that every relative link and image in
every tracked *.md file points to an existing file or directory, and that every
``#anchor`` resolves to a heading (GitHub slug rules) or an explicit
``<a id>``/``<a name>`` in the target Markdown file. External http(s) links are
skipped unless ``--external`` is given, in which case each unique URL is
requested once (useful locally; CI keeps the default offline mode so builds do
not fail on third-party outages).

Usage: check_links.py [--external] [ROOT]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

LINK_RE = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_DEF_RE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?(\S+?)>?(?:\s+.*)?$", re.M)
HTML_ATTR_RE = re.compile(r"""<(?:a|img|source)\b[^>]*?\b(?:href|src|srcset)\s*=\s*["']([^"']+)["']""", re.I)
HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$", re.M)
HTML_ANCHOR_RE = re.compile(r"""<a\b[^>]*\b(?:id|name)\s*=\s*["']([^"']+)["']""", re.I)
FENCE_RE = re.compile(r"^(```|~~~).*?^\1\s*$", re.M | re.S)
INLINE_CODE_RE = re.compile(r"`[^`\n]*`")


def tracked_markdown(root: Path) -> list[Path]:
    try:
        out = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "*.md"],
            check=True,
            capture_output=True,
        ).stdout
        files = [root / p for p in out.decode().split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        files = [p for p in root.rglob("*.md") if ".git" not in p.parts]
    return sorted(p for p in files if p.exists())


def strip_code(text: str) -> str:
    text = FENCE_RE.sub("", text)
    return INLINE_CODE_RE.sub("", text)


def github_slug(heading: str) -> str:
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links -> text
    text = re.sub(r"<[^>]+>", "", text)  # inline HTML
    text = text.replace("`", "").replace("*", "")
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


_anchor_cache: dict[Path, set[str]] = {}


def anchors_for(md: Path) -> set[str]:
    if md not in _anchor_cache:
        text = FENCE_RE.sub("", md.read_text(encoding="utf-8"))
        seen: dict[str, int] = {}
        anchors: set[str] = set()
        for _hashes, title in HEADING_RE.findall(text):
            slug = github_slug(title)
            n = seen.get(slug, 0)
            anchors.add(slug if n == 0 else f"{slug}-{n}")
            seen[slug] = n + 1
        anchors.update(a.lower() for a in HTML_ANCHOR_RE.findall(text))
        _anchor_cache[md] = anchors
    return _anchor_cache[md]


def links_in(md: Path) -> list[str]:
    text = strip_code(md.read_text(encoding="utf-8"))
    found = LINK_RE.findall(text) + REF_DEF_RE.findall(text) + HTML_ATTR_RE.findall(text)
    return [f.split()[0] for f in found]


def check_external(url: str) -> str | None:
    req = urllib.request.Request(url, method="GET", headers={"User-Agent": "radiogeddon-linkcheck"})
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            if resp.status >= 400:
                return f"HTTP {resp.status}"
    except urllib.error.HTTPError as exc:
        return f"HTTP {exc.code}"
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        return str(exc)
    return None


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("root", nargs="?", default=Path(__file__).resolve().parent.parent, type=Path)
    ap.add_argument("--external", action="store_true", help="also request http(s) links")
    args = ap.parse_args(argv)
    root = args.root.resolve()

    errors: list[str] = []
    external: dict[str, list[str]] = {}
    files = tracked_markdown(root)
    for md in files:
        rel_md = md.relative_to(root)
        for link in links_in(md):
            if link.startswith(("http://", "https://")):
                external.setdefault(link, []).append(str(rel_md))
                continue
            if link.startswith(("mailto:", "tel:", "data:")):
                continue
            path_part, _, anchor = link.partition("#")
            if path_part:
                target = (root / path_part.lstrip("/")) if path_part.startswith("/") else (md.parent / path_part)
                target = target.resolve()
                if not target.exists():
                    errors.append(f"{rel_md}: missing target {link}")
                    continue
            else:
                target = md
            if anchor and target.suffix.lower() == ".md":
                if anchor.lower() not in anchors_for(target):
                    errors.append(f"{rel_md}: unknown anchor #{anchor} in {target.relative_to(root)}")

    if args.external:
        for url, where in sorted(external.items()):
            problem = check_external(url)
            if problem:
                errors.append(f"{where[0]}: {url} -> {problem}")

    for e in errors:
        print(f"BROKEN {e}")
    checked = sum(len(links_in(m)) for m in files)
    print(f"checked {checked} links in {len(files)} Markdown files: {len(errors)} broken")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
