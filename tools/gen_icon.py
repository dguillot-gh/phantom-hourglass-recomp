#!/usr/bin/env python3
"""Draws the program's icon (frontend/res/app.ico): a simple gold hourglass on a sea-blue rounded square.

Original art made from shapes in this script (no game artwork). Pure Python, no image library:

    python tools/gen_icon.py frontend/res/app.ico
    python tools/gen_icon.py --android android/app/src/main/res     (the launcher icons, mipmap-*/ic_launcher.png)
"""
import struct
import sys
import zlib

SIZES = (16, 24, 32, 48, 64, 128, 256)


def inside_rounded_square(x, y, r):
    cx = min(max(x, r), 1 - r)
    cy = min(max(y, r), 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def color_at(x, y):
    """RGBA of the icon at (x, y) in 0..1 (y down)."""
    if not inside_rounded_square(x, y, 0.18):
        return (0, 0, 0, 0)
    sea_top, sea_bottom = (40, 140, 220), (12, 60, 140)
    t = y
    bg = tuple(int(a + (b - a) * t) for a, b in zip(sea_top, sea_bottom))
    gold, gold_dark, sand = (240, 200, 90), (170, 120, 40), (250, 230, 170)
    # Frame: a bar at the top and the bottom.
    if 0.20 <= x <= 0.80 and (0.14 <= y <= 0.22 or 0.78 <= y <= 0.86):
        return (*gold, 255)
    # Glass: two triangles meeting at the middle (width shrinks towards y = 0.5).
    half = abs(y - 0.5)
    if 0.22 < y < 0.78:
        w = 0.04 + half * 0.92
        if abs(x - 0.5) <= w:
            edge = abs(x - 0.5) > w - 0.035
            if edge:
                return (*gold_dark, 255)
            # Sand: the bottom bulb is mostly full, the top has a little left.
            if y > 0.58 or (y < 0.5 and y > 0.40):
                return (*sand, 255)
            if y >= 0.5 and abs(x - 0.5) < 0.02:  # the falling stream
                return (*sand, 255)
            return (200, 230, 255, 255)
    return (*bg, 255)


def render(size):
    ss = 4  # supersampling for smooth edges
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    r, g, b, a = color_at((px + (sx + 0.5) / ss) / size, (py + (sy + 0.5) / ss) / size)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = ss * ss
            a = acc[3] / n
            if a:
                row += bytes((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(a)))
            else:
                row += bytes(4)
        rows.append(bytes(row))
    return rows


def png(size, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


ANDROID = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--android":
        import os
        for density, size in ANDROID.items():
            d = os.path.join(sys.argv[2], "mipmap-" + density)
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, "ic_launcher.png"), "wb") as f:
                f.write(png(size, render(size)))
        print(f"wrote {len(ANDROID)} launcher icons under {sys.argv[2]}")
        return
    out = sys.argv[1] if len(sys.argv) > 1 else "frontend/res/app.ico"
    images = [png(s, render(s)) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for s, data in zip(SIZES, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(out, "wb") as f:
        f.write(header + entries + b"".join(images))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
