#!/usr/bin/env python3
"""Draw the application icon and write it out in the shapes Windows and macOS want.

The artwork here is drawn from scratch by this script. None of it comes out of the game's own
resource fork - that artwork is Ambrosia Software's and is deliberately not in this repository -
so what this produces is a generic segmented-creature motif that happens to suit the game, and
nothing that belongs to anybody else.

    python3 tools/make_icons.py            # writes into packaging/

Produces packaging/apeiron.png (1024), apeiron.ico (Windows) and apeiron.icns (macOS). The .rc
file beside them is what points the Windows linker at the .ico; it is checked in and not
generated. Re-run this only to change the drawing; the results are committed so that a build
needs neither Pillow nor this script.
"""
import math
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    sys.exit("this needs Pillow:  pip install pillow")

S = 1024                      # drawn at 1024 and reduced; everything below is in these units
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "packaging")


def disc(d, cx, cy, r, fill, rim=None, rim_w=0):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill,
              outline=rim, width=rim_w)


def draw_icon():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    # The rounded tile, dark with a little depth to it so the discs have something to sit on.
    pad, rad = int(S * 0.055), int(S * 0.22)
    d.rounded_rectangle([pad, pad, S - pad, S - pad], rad, fill=(14, 18, 26, 255))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([S * 0.10, S * 0.02, S * 0.95, S * 0.78],
                                 fill=(30, 70, 60, 255))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.13))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([pad, pad, S - pad, S - pad], rad, fill=255)
    im.paste(Image.alpha_composite(im, glow), (0, 0), mask)
    d = ImageDraw.Draw(im)

    # A body curving down the tile: seven segments on an arc, the head largest and in front.
    cx, cy, R = S * 0.30, S * 0.30, S * 0.42
    seg = [(0.00, 0.95), (0.17, 0.97), (0.34, 1.00), (0.51, 1.02),
           (0.67, 1.03), (0.82, 1.03), (0.96, 1.02)]
    body_hi, body_lo = (86, 196, 92), (28, 104, 44)
    for i, (t, rr) in enumerate(reversed(seg)):
        a = math.radians(8 + t * 74)
        x, y = cx + math.cos(a) * R * rr, cy + math.sin(a) * R * rr
        r = S * (0.082 if i < len(seg) - 1 else 0.098)
        k = i / float(len(seg) - 1)
        col = tuple(int(body_lo[c] + (body_hi[c] - body_lo[c]) * k) for c in range(3))
        disc(d, x, y, r, col + (255,), (10, 42, 18, 255), max(2, int(S * 0.008)))
        disc(d, x - r * 0.30, y - r * 0.34, r * 0.30, (255, 255, 255, 70))   # specular
        if i == len(seg) - 1:                                   # the head's two eyes
            for s in (-1, 1):
                ex, ey = x + s * r * 0.34, y - r * 0.12
                disc(d, ex, ey, r * 0.27, (250, 250, 240, 255))
                disc(d, ex + s * r * 0.06, ey + r * 0.04, r * 0.13, (18, 18, 22, 255))

    # The shooter, bottom left, and a shot on its way up - the crystal is a plain diamond, which
    # is about as generic as a shape gets.
    sx, sy, s2 = S * 0.255, S * 0.825, S * 0.075
    d.polygon([(sx, sy - s2), (sx + s2 * 0.8, sy), (sx, sy + s2 * 0.7), (sx - s2 * 0.8, sy)],
              fill=(120, 215, 255, 255), outline=(22, 76, 110, 255), width=max(2, int(S * 0.007)))
    d.polygon([(sx, sy - s2 * 0.5), (sx + s2 * 0.34, sy - s2 * 0.08), (sx, sy + s2 * 0.2),
               (sx - s2 * 0.34, sy - s2 * 0.08)], fill=(226, 248, 255, 230))
    d.rounded_rectangle([sx - S * 0.016, sy - S * 0.235, sx + S * 0.016, sy - S * 0.135],
                        S * 0.016, fill=(255, 226, 92, 255))

    return im


def write_icns(im, path):
    """A PNG-carrying .icns, which is what every macOS since 10.7 reads.

    The container is a four-byte magic, a big-endian total length, then one chunk per size: a
    four-character type, the chunk's length including its own eight-byte header, and the PNG.
    """
    kinds = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128),
             (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024), (b"ic11", 32),
             (b"ic12", 64), (b"ic13", 256), (b"ic14", 512)]
    import io
    chunks = b""
    for tag, size in kinds:
        buf = io.BytesIO()
        im.resize((size, size), Image.LANCZOS).save(buf, format="PNG")
        png = buf.getvalue()
        chunks += tag + struct.pack(">I", len(png) + 8) + png
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


def main():
    os.makedirs(OUT, exist_ok=True)
    im = draw_icon()
    im.save(os.path.join(OUT, "apeiron.png"))
    im.resize((256, 256), Image.LANCZOS).save(
        os.path.join(OUT, "apeiron.ico"),
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    write_icns(im, os.path.join(OUT, "apeiron.icns"))
    for n in ("apeiron.png", "apeiron.ico", "apeiron.icns"):
        p = os.path.join(OUT, n)
        print("%-16s %8d bytes" % (n, os.path.getsize(p)))


if __name__ == "__main__":
    main()
