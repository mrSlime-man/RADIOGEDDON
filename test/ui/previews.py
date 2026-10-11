#!/usr/bin/env python3
"""Turn the UI harness's PBM screens into enlarged PNGs and one contact sheet.

    previews.py DIR

Every image is a SYNTHETIC development preview (drawing code fed with
synthetic data on the host), not a screenshot of a Flipper Zero; the files
are named synthetic_* and the sheet says so. Never use them as Apps Catalog
screenshots, which must come from qFlipper on a real device.
Standard library only.
"""
import os
import struct
import sys
import zlib

SCALE = 4
FG = (0, 0, 0)
BG = (255, 140, 0)  # the Flipper's orange backlight


def read_pbm(path):
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    w, h = int(fields[1]), int(fields[2])
    stride = (w + 7) // 8
    rows = []
    for y in range(h):
        row = data[pos + y * stride:pos + (y + 1) * stride]
        rows.append([(row[x // 8] >> (7 - x % 8)) & 1 for x in range(w)])
    return w, h, rows


def write_png(path, w, h, pixels):
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def scaled(rows, scale):
    out = []
    for row in rows:
        line = []
        for v in row:
            line.extend([FG if v else BG] * scale)
        out.extend([line] * scale)
    return out


def main(argv):
    d = argv[1]
    names = sorted(n for n in os.listdir(d) if n.endswith(".pbm"))
    sheet, gap = [], 8
    for n in names:
        w, h, rows = read_pbm(os.path.join(d, n))
        img = scaled(rows, SCALE)
        write_png(os.path.join(d, n[:-4] + ".png"), w * SCALE, h * SCALE, img)
        sheet.append(img)
    if sheet:
        cols = 4
        cw, ch = 128 * SCALE, 64 * SCALE
        rows_n = (len(sheet) + cols - 1) // cols
        W, H = cols * cw + (cols + 1) * gap, rows_n * ch + (rows_n + 1) * gap
        canvas = [[(255, 255, 255)] * W for _ in range(H)]
        for i, img in enumerate(sheet):
            ox = gap + (i % cols) * (cw + gap)
            oy = gap + (i // cols) * (ch + gap)
            for y, line in enumerate(img):
                canvas[oy + y][ox:ox + cw] = line
        write_png(os.path.join(d, "synthetic_contact_sheet.png"), W, H, canvas)
    print("%d synthetic previews in %s (not device screenshots)" % (len(names), d))


if __name__ == "__main__":
    main(sys.argv)
