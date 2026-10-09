#!/usr/bin/env python3
"""Generate RadioGeddon brand assets.

Outputs (committed, so nobody needs to run this to view the repo):
    docs/assets/brand/logo.svg            256x256 emblem on dark tile
    docs/assets/brand/logo-mark.svg       emblem without the tile (transparent)
    docs/assets/brand/banner.svg          1280x360 README header
    docs/assets/brand/social-preview.svg  1280x640 GitHub social preview source
    assets/icon_10px.png                  10x10 1-bit Flipper launcher icon

Text is converted to vector outlines (Inter, SIL Open Font License; DejaVu Sans
Mono) so the artwork renders identically on GitHub regardless of the viewer's
installed fonts. Render social-preview.svg to PNG with tools/brand/render_png.mjs.

Requires: python3, fonttools, Pillow; fonts below (override with env vars).
"""

from __future__ import annotations

import math
import os
import re
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "assets" / "brand"

FONT_DISPLAY = os.environ.get("RG_FONT_DISPLAY", "/usr/share/fonts/opentype/inter/InterDisplay-ExtraBold.otf")
FONT_TEXT = os.environ.get("RG_FONT_TEXT", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")
FONT_MONO = os.environ.get("RG_FONT_MONO", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf")

# Palette
BG0 = "#05080F"
BG1 = "#0A1222"
TILE = "#0B1426"
GRID = "#1B2C4A"
INK = "#E8EEF8"
MUTED = "#8FA3C2"
CYAN = "#22D3EE"
CYAN_DEEP = "#0891B2"
PURPLE = "#A855F7"
VIOLET = "#7C3AED"


class Outliner:
    """Turn a string into one SVG path using a font's glyph outlines."""

    def __init__(self, path: str):
        self.font = TTFont(path)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font["head"].unitsPerEm
        self.hmtx = self.font["hmtx"]

    def width(self, text: str, size: float, tracking: float = 0.0) -> float:
        scale = size / self.upm
        total = 0.0
        for ch in text:
            gname = self.cmap.get(ord(ch), ".notdef")
            total += self.hmtx[gname][0] * scale + tracking
        return total - tracking if text else 0.0

    def path(self, text: str, x: float, y: float, size: float, tracking: float = 0.0) -> str:
        scale = size / self.upm
        pen = SVGPathPen(self.glyphs)
        cursor = x
        for ch in text:
            gname = self.cmap.get(ord(ch), ".notdef")
            tpen = TransformPen(pen, (scale, 0, 0, -scale, cursor, y))
            self.glyphs[gname].draw(tpen)
            cursor += self.hmtx[gname][0] * scale + tracking
        return _round_path(pen.getCommands())


def _round_path(d: str) -> str:
    return re.sub(r"-?\d+\.\d+", lambda m: fmt(float(m.group(0)), 1), d)


def fmt(v: float, digits: int = 2) -> str:
    out = f"{v:.{digits}f}".rstrip("0").rstrip(".")
    return "0" if out in ("-0", "") else out


def defs(prefix: str) -> str:
    return f"""
  <defs>
    <linearGradient id="{prefix}sig" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="{CYAN}"/>
      <stop offset="0.55" stop-color="#60A5FA"/>
      <stop offset="1" stop-color="{PURPLE}"/>
    </linearGradient>
    <linearGradient id="{prefix}edge" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{CYAN}" stop-opacity="0.85"/>
      <stop offset="1" stop-color="{VIOLET}" stop-opacity="0.85"/>
    </linearGradient>
    <radialGradient id="{prefix}glow" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0" stop-color="{CYAN}" stop-opacity="0.35"/>
      <stop offset="1" stop-color="{CYAN}" stop-opacity="0"/>
    </radialGradient>
  </defs>"""


def emblem_waveform(x0: float, y0: float, s: float) -> str:
    """OOK pulse train resolving into a sine wave, in a 256-unit design box."""

    def p(x, y):
        return f"{fmt(x0 + x * s)},{fmt(y0 + y * s)}"

    hi, lo = WAVE_HI, WAVE_LO
    pts = [(38, lo), (58, lo), (58, hi), (74, hi), (74, lo), (96, lo), (96, hi), (132, hi), (132, lo)]
    d = "M" + " L".join(p(x, y) for x, y in pts)
    # Sine segment from x=132 to x=WAVE_END_X: starts on the low rail (continuous
    # with the pulse train) and settles on the centre line, where the marker sits.
    for i in range(1, WAVE_STEPS + 1):
        x, y = wave_point(i / WAVE_STEPS)
        d += f" L{p(x, y)}"
    return d


WAVE_HI, WAVE_LO, WAVE_END_X, WAVE_STEPS = 92, 150, 214, 48


def wave_point(t: float) -> tuple[float, float]:
    mid = (WAVE_HI + WAVE_LO) / 2
    amp = (WAVE_LO - mid) * (1 - 0.3 * t)
    return 132 + (WAVE_END_X - 132) * t, mid + amp * math.cos(math.pi * 2.5 * t)


def emblem(prefix: str, x0: float, y0: float, size: float, tile: bool) -> str:
    s = size / 256.0
    parts = []
    if tile:
        parts.append(
            f'<rect x="{fmt(x0 + 4 * s)}" y="{fmt(y0 + 4 * s)}" width="{fmt(248 * s)}" height="{fmt(248 * s)}" '
            f'rx="{fmt(56 * s)}" fill="{TILE}" stroke="url(#{prefix}edge)" stroke-width="{fmt(3 * s)}"/>'
        )
        parts.append(
            f'<circle cx="{fmt(x0 + 128 * s)}" cy="{fmt(y0 + 121 * s)}" r="{fmt(104 * s)}" fill="url(#{prefix}glow)"/>'
        )
    # Graticule
    grid = []
    for g in range(48, 209, 40):
        grid.append(f"M{fmt(x0 + g * s)},{fmt(y0 + 52 * s)} V{fmt(y0 + 204 * s)}")
        grid.append(f"M{fmt(x0 + 44 * s)},{fmt(y0 + (g + 4) * s)} H{fmt(x0 + 212 * s)}")
    parts.append(
        f'<path d="{" ".join(grid)}" stroke="{GRID}" stroke-width="{fmt(1.6 * s)}" fill="none" opacity="0.9"/>'
    )
    parts.append(
        f'<path d="{emblem_waveform(x0, y0, s)}" fill="none" stroke="url(#{prefix}sig)" '
        f'stroke-width="{fmt(13 * s)}" stroke-linecap="round" stroke-linejoin="round"/>'
    )
    # Peak marker on the decoded wave
    ex, ey = wave_point(1.0)
    parts.append(
        f'<circle cx="{fmt(x0 + ex * s)}" cy="{fmt(y0 + ey * s)}" r="{fmt(10 * s)}" fill="{PURPLE}" '
        f'stroke="{TILE}" stroke-width="{fmt(3 * s)}"/>'
    )
    # Baseline ticks (spectrum hint)
    ticks = []
    for i, h in enumerate([10, 18, 12, 26, 16, 34, 22, 14, 28, 18, 10, 20]):
        x = 54 + i * 13.5
        ticks.append(f"M{fmt(x0 + x * s)},{fmt(y0 + 212 * s)} V{fmt(y0 + (212 - h) * s)}")
    parts.append(
        f'<path d="{" ".join(ticks)}" stroke="url(#{prefix}sig)" stroke-width="{fmt(5 * s)}" '
        f'stroke-linecap="round" opacity="0.55"/>'
    )
    return "\n  ".join(parts)


def svg_doc(w: int, h: int, body: str, title: str, prefix: str) -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
        f'role="img" aria-labelledby="{prefix}title">\n'
        f'  <title id="{prefix}title">{title}</title>{defs(prefix)}\n  {body}\n</svg>\n'
    )


def spectrum_bars(x: float, y_base: float, width: float, max_h: float, n: int, prefix: str, opacity: float) -> str:
    bars = []
    step = width / n
    for i in range(n):
        t = i / n
        # Deterministic "spectrum": a few carriers over a noise floor.
        h = 0.10 + 0.08 * (0.5 + 0.5 * math.sin(i * 1.7) * math.cos(i * 0.37))
        for c, w in ((0.18, 0.9), (0.47, 0.55), (0.72, 1.0), (0.86, 0.4)):
            h += w * math.exp(-((t - c) ** 2) / 0.00045)
        h = min(h, 1.0)
        bars.append(
            f'<rect x="{fmt(x + i * step)}" y="{fmt(y_base - h * max_h)}" width="{fmt(step * 0.62)}" '
            f'height="{fmt(h * max_h)}" rx="{fmt(step * 0.2)}"/>'
        )
    return f'<g fill="url(#{prefix}sig)" opacity="{opacity}">' + "".join(bars) + "</g>"


def background(w: int, h: int, prefix: str, grid_step: int) -> str:
    lines = []
    for gx in range(grid_step, w, grid_step):
        lines.append(f"M{gx},0 V{h}")
    for gy in range(grid_step, h, grid_step):
        lines.append(f"M0,{gy} H{w}")
    return (
        f'<linearGradient id="{prefix}bg" x1="0" y1="0" x2="0" y2="1">'
        f'<stop offset="0" stop-color="{BG1}"/><stop offset="1" stop-color="{BG0}"/></linearGradient>'
        f'<rect width="{w}" height="{h}" fill="url(#{prefix}bg)"/>'
        f'<path d="{" ".join(lines)}" stroke="{GRID}" stroke-width="1" opacity="0.35"/>'
    )


def pulse_train(x: float, y: float, width: float, height: float, prefix: str) -> str:
    # A Princeton-style OOK frame: short-high/long-low = 0, long-high/short-low = 1.
    bits = "0110100111010010"
    unit = width / (len(bits) * 4)
    d = f"M{fmt(x)},{fmt(y + height)}"
    cx = x
    for b in bits:
        hi = 3 if b == "1" else 1
        d += f" V{fmt(y)} H{fmt(cx + hi * unit)} V{fmt(y + height)} H{fmt(cx + 4 * unit)}"
        cx += 4 * unit
    return (
        f'<path d="{d}" fill="none" stroke="url(#{prefix}sig)" stroke-width="3" '
        f'stroke-linejoin="round" opacity="0.75"/>'
    )


def build_banner(disp: Outliner, text: Outliner, mono: Outliner) -> str:
    w, h, p = 1280, 360, "b"
    body = [background(w, h, p, 40)]
    body.append(spectrum_bars(0, h, w, 58, 128, p, 0.2))
    body.append(pulse_train(872, 34, 340, 26, p))
    body.append(emblem(p, 72, 72, 216, tile=True))
    # Wordmark: "Radio" ink + "Geddon" gradient
    size, track = 92, 1.5
    x, base = 330, 192
    radio_w = disp.width("Radio", size, track)
    body.append(f'<path d="{disp.path("Radio", x, base, size, track)}" fill="{INK}"/>')
    body.append(f'<path d="{disp.path("Geddon", x + radio_w + track, base, size, track)}" fill="url(#{p}sig)"/>')
    body.append(
        f'<path d="{text.path("Standalone Sub-GHz signal analysis for Flipper Zero", x + 4, 246, 31)}" fill="{MUTED}"/>'
    )
    body.append(
        f'<path d="{mono.path("SCAN · HOP · CAPTURE · DECODE · ANALYZE · COMPARE · REPLAY", x + 4, 292, 17, 1.2)}" '
        f'fill="{CYAN}" opacity="0.9"/>'
    )
    return svg_doc(w, h, "\n  ".join(body), "RadioGeddon — standalone Sub-GHz signal analysis for Flipper Zero", p)


def build_social(disp: Outliner, text: Outliner, mono: Outliner) -> str:
    w, h, p = 1280, 640, "s"
    body = [background(w, h, p, 40)]
    body.append(spectrum_bars(0, h, w, 170, 128, p, 0.25))
    body.append(emblem(p, 100, 150, 260, tile=True))
    size, track = 104, 1.5
    x = 410
    radio_w = disp.width("Radio", size, track)
    body.append(f'<path d="{disp.path("Radio", x, 285, size, track)}" fill="{INK}"/>')
    body.append(f'<path d="{disp.path("Geddon", x + radio_w + track, 285, size, track)}" fill="url(#{p}sig)"/>')
    body.append(f'<path d="{text.path("Standalone Sub-GHz signal analysis", x + 4, 345, 36)}" fill="{MUTED}"/>')
    body.append(f'<path d="{text.path("for Flipper Zero", x + 4, 392, 36)}" fill="{MUTED}"/>')
    body.append(
        f'<path d="{mono.path("OFFICIAL · UNLEASHED · ROGUEMASTER", x + 4, 448, 20, 1.5)}" fill="{CYAN}" opacity="0.9"/>'
    )
    body.append(pulse_train(100, 70, 1080, 34, p))
    return svg_doc(w, h, "\n  ".join(body), "RadioGeddon — Sub-GHz signal analysis for Flipper Zero", p)


def build_logo(tile: bool) -> str:
    p = "l" if tile else "m"
    return svg_doc(256, 256, emblem(p, 0, 0, 256, tile=tile), "RadioGeddon logo", p)


# 10x10 launcher icon: broadcast mast with two pairs of radiating arcs.
ICON_10 = [
    "..........",
    ".#......#.",
    "#..#..#..#",
    "#.#.##.#.#",
    "#.#.##.#.#",
    "#..#..#..#",
    ".#..##..#.",
    "....##....",
    "...#..#...",
    "..#....#..",
]


def build_icon(path: Path) -> None:
    from PIL import Image

    img = Image.new("1", (10, 10), 1)
    for yy, row in enumerate(ICON_10):
        for xx, ch in enumerate(row):
            if ch == "#":
                img.putpixel((xx, yy), 0)
    img.save(path)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    disp, text, mono = Outliner(FONT_DISPLAY), Outliner(FONT_TEXT), Outliner(FONT_MONO)
    (OUT / "logo.svg").write_text(build_logo(tile=True))
    (OUT / "logo-mark.svg").write_text(build_logo(tile=False))
    (OUT / "banner.svg").write_text(build_banner(disp, text, mono))
    (OUT / "social-preview.svg").write_text(build_social(disp, text, mono))
    build_icon(ROOT / "assets" / "icon_10px.png")
    for f in sorted(OUT.glob("*.svg")):
        print(f"{f.relative_to(ROOT)}  {f.stat().st_size} bytes")


if __name__ == "__main__":
    main()
