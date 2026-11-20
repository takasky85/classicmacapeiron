#!/usr/bin/env python3
"""Make the application icon and write it out in the shapes Windows and macOS want.

Two sources, and which one you get depends on the arguments:

    python3 tools/make_icons.py --from-game <the installed Apeiron folder>

takes the real thing - `icl8` 128 and its `ICN#` mask out of the application's resource fork,
through `clut` 130, which is the 32x32 icon the Finder puts on Apeiron itself. That is Ambrosia
Software's artwork. The committed icons were made this way, deliberately, because the point is
for the port to look like the game it is a port of. It is the one piece of their artwork in this
repository; everything else still has to come from your own copy.

    python3 tools/make_icons.py

with no arguments draws a generic segmented-creature motif from scratch instead, which belongs
to nobody and is here so the build has something to fall back on.

Either way it produces packaging/apeiron.png, apeiron.ico (Windows) and apeiron.icns (macOS).
The .rc file beside them points the Windows linker at the .ico; it is checked in, not generated.
The results are committed, so a build needs neither Pillow nor this script.
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


def from_game(folder):
    """The application's own Finder icon, out of its resource fork.

    icl8 is 32x32 bytes of indices into clut 130 - the Mac's system palette, which the resource
    fork carries a copy of - and the second half of ICN# is the 1-bit mask that says which of
    those pixels are actually the icon. ics8/ics# are the same thing at 16x16, and worth taking
    separately rather than shrinking the big one, because somebody drew it by hand.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rsrc
    app = None
    for cand in (os.path.join(folder, ".rsrc", "Apeiron"), os.path.join(folder, "Apeiron"), folder):
        if os.path.isfile(cand):
            try:
                res = {(t, i): d for t, i, n, a, d in rsrc.parse(cand)}
            except Exception:
                continue
            if ("icl8", 128) in res:
                app = res
                break
    if not app:
        sys.exit("no icl8 128 in %s - point this at the folder holding the Apeiron application"
                 % folder)

    clut = app[("clut", 130)]
    count = struct.unpack(">H", clut[6:8])[0] + 1
    pal = [(0, 0, 0)] * 256
    for k in range(count):
        o = 8 + k * 8
        v, r, g, b = struct.unpack(">HHHH", clut[o:o + 8])
        if v < 256:
            pal[v] = (r >> 8, g >> 8, b >> 8)

    def one(px_tag, mask_tag, size):
        px, mk = app[(px_tag, 128)], app[(mask_tag, 128)]
        half = size * size // 8                      # the icon comes first, then the mask
        im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        p = im.load()
        for y in range(size):
            for x in range(size):
                bit = (mk[half + y * (size // 8) + x // 8] >> (7 - (x % 8))) & 1
                if bit:
                    r, g, b = pal[px[y * size + x]]
                    p[x, y] = (r, g, b, 255)
        return im

    return one("icl8", "ICN#", 32), one("ics8", "ics#", 16)


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


def write_ico(at, path, sizes=(16, 32, 64, 128, 256)):
    """An .ico carrying a PNG per size.

    Pillow's own ICO writer takes one image and resizes it to each size with a smooth filter,
    which is the one thing this icon must not have done to it - every size here is a whole
    multiple of a 32x32 original and has to stay square-edged. Windows has read PNG-payload
    icons since Vista, so writing the container by hand is a few lines and keeps the pixels.
    """
    import io
    entries, blobs, offset = [], [], 6 + 16 * len(sizes)
    for size in sizes:
        buf = io.BytesIO()
        at(size).save(buf, format="PNG")
        png = buf.getvalue()
        entries.append(struct.pack("<BBBBHHII",
                                   0 if size >= 256 else size,   # 0 means 256
                                   0 if size >= 256 else size,
                                   0, 0, 1, 32, len(png), offset))
        blobs.append(png)
        offset += len(png)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(sizes)))
        for e in entries:
            f.write(e)
        for b in blobs:
            f.write(b)


def write_icns_sizes(at, path):
    """A PNG-carrying .icns, which is what every macOS since 10.7 reads.

    The container is a four-byte magic, a big-endian total length, then one chunk per size: a
    four-character type, the chunk's length including its own eight-byte header, and the PNG.
    """
    import io
    kinds = [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128),
             (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024), (b"ic11", 32),
             (b"ic12", 64), (b"ic13", 256), (b"ic14", 512)]
    chunks = b""
    for tag, size in kinds:
        buf = io.BytesIO()
        at(size).save(buf, format="PNG")
        png = buf.getvalue()
        chunks += tag + struct.pack(">I", len(png) + 8) + png
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(chunks) + 8) + chunks)


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
    game = None
    if len(sys.argv) > 2 and sys.argv[1] == "--from-game":
        game = sys.argv[2]

    if game:
        # The real icon is 32x32 and that is all there ever was, so every larger size is a whole
        # multiple of it with no smoothing - a 32x32 icon blown up smoothly just looks like a
        # mistake, where blown up square looks like what it is.
        big, small = from_game(game)
        im = big.resize((1024, 1024), Image.NEAREST)
        sizes16 = {16: small}
        print("icon: Apeiron's own, from its resource fork")
    else:
        im = draw_icon()
        sizes16 = {}
        print("icon: the drawn fallback")

    def at(size):
        if size in sizes16:
            return sizes16[size]
        src = big if game else im
        return src.resize((size, size), Image.NEAREST if game else Image.LANCZOS)

    im.save(os.path.join(OUT, "apeiron.png"))
    write_ico(at, os.path.join(OUT, "apeiron.ico"))
    write_icns_sizes(at, os.path.join(OUT, "apeiron.icns"))
    for n in ("apeiron.png", "apeiron.ico", "apeiron.icns"):
        p = os.path.join(OUT, n)
        print("%-16s %8d bytes" % (n, os.path.getsize(p)))


if __name__ == "__main__":
    main()
