#!/usr/bin/env python3
"""Extract the assets the Apeiron startup/title sequence needs, in SDL3-friendly formats.

usage: extract_assets.py <the game's .zip | its folder | a MacBinary file> [out_dir]
  A .zip is unpacked for you, into a temporary folder that is removed afterwards. Whatever you give it,
  the result goes in out_dir, which is "apeiron_classic_assets" beside the input if you leave it out.
  The folder, once unpacked, is the Apeiron one: 'Apeiron Titles', 'Apeiron Scores', ... and the hidden
  .rsrc/ folder with their resource forks.
Needs Pillow (pip install pillow). Fonts come from source_fonts/ (resource forks of the Mac font suitcases)
and from the game's own embedded New York bitmaps.
"""
import os, struct, sys, wave, collections, tempfile, zipfile, shutil, stat
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rsrc, pict, unpack_macbinary
from PIL import Image

# ----------------------------------------------------------------------------- palettes / pictures
def clut(d):
    """'clut' resource -> list of 256 (r,g,b) 8-bit tuples (high byte of the 16-bit components)."""
    seed, flags, n = struct.unpack('>IHH', d[:8])
    pal = [(0, 0, 0)] * 256
    for i in range(n + 1):
        v, r, g, b = struct.unpack('>HHHH', d[8+8*i:16+8*i])
        pal[v & 255] = (r >> 8, g >> 8, b >> 8)
    return pal

def remap_to_palette(src_colors, palette):
    """src index -> palette index. Exact RGB match if there is one (prefer the same index), else the nearest
    entry - which is what QuickDraw does when it draws into a window whose colour table is `palette`."""
    by_rgb = collections.defaultdict(list)
    for i, c in enumerate(palette): by_rgb[c].append(i)
    remap, exact, dist = {}, 0, []
    for k, c in src_colors.items():
        if c in by_rgb:
            remap[k] = k if k in by_rgb[c] else by_rgb[c][0]; exact += 1
        else:
            j = min(range(256), key=lambda j: sum((a-b)**2 for a, b in zip(palette[j], c)))
            remap[k] = j; dist.append(sum((a-b)**2 for a, b in zip(palette[j], c)) ** 0.5)
    return remap, exact, dist

def save_indexed(w, h, data, palette, path):
    img = Image.frombytes('P', (w, h), data)
    img.putpalette([v for c in palette for v in c])
    img.save(path, format='BMP')

def indexed_bmp(pict_data, palette, path):
    im = pict.decode_indexed(pict_data)
    if im['ctab']:
        ctab = {k: (c[0], c[1], c[2]) for k, c in im['ctab'].items()}
    else:                                  # a plain 1-bit BitMap: 0 is white, 1 is black
        ctab = {0: (255, 255, 255), 1: (0, 0, 0)}
    remap, _, _ = remap_to_palette(ctab, palette)
    save_indexed(im['w'], im['h'], bytes(remap[b] for b in im['indices']), palette, path)
    return im['w'], im['h']

def parse_ppat(d):
    """'ppat' (colour pixel pattern) -> (w, h, indices, {index: rgb8})."""
    ptype, pmap, pdata = struct.unpack('>HII', d[:10])
    base, rb, top, left, bot, right = struct.unpack('>IHhhhh', d[pmap:pmap+14])
    tab = struct.unpack('>I', d[pmap+42:pmap+46])[0]
    w, h, rowb = right - left, bot - top, rb & 0x3FFF
    px = b''.join(d[pdata + y*rowb: pdata + y*rowb + w] for y in range(h))
    seed, fl, n = struct.unpack('>IHH', d[tab:tab+8]); ct = {}
    for i in range(n + 1):
        v, r, g, b = struct.unpack('>HHHH', d[tab+8+8*i: tab+16+8*i])
        ct[i if fl & 0x8000 else v] = (r >> 8, g >> 8, b >> 8)
    return w, h, px, ct

def pattern_bmp(d, palette, path):
    w, h, px, ct = parse_ppat(d)
    used = {k: ct[k] for k in set(px)}
    remap, exact, dist = remap_to_palette(used, palette)
    save_indexed(w, h, bytes(remap[b] for b in px), palette, path)
    return w, h, len(used), exact, (max(dist) if dist else 0)

# ----------------------------------------------------------------------------- bitmap fonts (NFNT)
def nfnt_to_font(nfnt, stem):
    """Write <stem>.bmp (glyph strip, ink = index 1) and <stem>.fnt (metrics for the 256 Mac Roman codes).
    .fnt: 'FONT ascent descent height' then 256 lines 'x w left advance'
      x = glyph's left edge in the strip, w = image width, left = pen->image offset (kernMax + offset), advance = pen advance."""
    ft, fc, lc, wm, km, nd, frw, frh, ow, asc, desc, lead, rw = struct.unpack('>hhhhhhhhHhhhh', nfnt[:26])
    n = lc - fc + 3
    loc_off = 26 + rw * 2 * frh
    loc = struct.unpack('>%dH' % n, nfnt[loc_off:loc_off + 2*n])
    owt = struct.unpack('>%dH' % (n-1), nfnt[16 + ow*2: 16 + ow*2 + 2*(n-1)]) + (0xFFFF,)   # the OW table has no end entry
    bits = nfnt[26:26 + rw*2*frh]
    W = rw * 16
    img = Image.new('P', (W, frh), 0)
    px = img.load()
    for y in range(frh):
        row = bits[y*rw*2:(y+1)*rw*2]
        for x in range(W):
            if (row[x >> 3] >> (7 - (x & 7))) & 1: px[x, y] = 1
    img.putpalette([0, 0, 0, 255, 255, 255] + [0] * 762)
    img.save(stem + '.bmp', format='BMP')
    lines = ['FONT %d %d %d' % (asc, desc, frh)]
    missing = lc - fc + 1
    for c in range(256):
        idx = c - fc if fc <= c <= lc else missing
        if owt[idx] == 0xFFFF: idx = missing
        x, w = loc[idx], loc[idx+1] - loc[idx]
        offset, adv = owt[idx] >> 8, owt[idx] & 0xFF
        lines.append('%d %d %d %d' % (x, w, km + offset, adv))
    open(stem + '.fnt', 'w').write('\n'.join(lines) + '\n')
    return W, frh, asc

def ttf_native_size(ttf):
    """The pixel size at which a pixel-style TrueType font is drawn on a whole-pixel grid: the
    smallest size whose drawing is exactly what the same text at twice the size shrinks to."""
    from PIL import ImageDraw, ImageFont
    def draw(size):
        f = ImageFont.truetype(ttf, size)
        im = Image.new('1', (size * 40, size * 3), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = '1'
        d.text((size, 0), 'HEgxo,AMw', font=f, fill=1)
        return im
    for size in range(5, 33):
        a, b = draw(size), draw(size * 2)
        if a.getbbox() and b.resize(a.size, Image.NEAREST).tobytes() == a.tobytes() \
                and a.resize(b.size, Image.NEAREST).tobytes() == b.tobytes():
            return size
    return 8


def ttf_to_font(ttf, stem, ascent=None, descent=None):
    """Make a font in the same two files nfnt_to_font writes from a pixel-style TrueType font, for
    when the real Mac font is not to hand. The glyphs are drawn at the font's own pixel size, with
    no smoothing. `ascent` and `descent` are the Mac font's, so that text sits on the same baseline
    and the same line height as the game was laid out for; the font's own are used if none."""
    from PIL import ImageDraw, ImageFont
    size = ttf_native_size(ttf)
    font = ImageFont.truetype(ttf, size)
    f_asc, f_desc = font.getmetrics()
    asc = f_asc if ascent is None else ascent
    desc = f_desc if descent is None else descent
    height = asc + desc
    dy = asc - f_asc                             # moves the font's baseline onto the Mac font's
    PAD = 4
    glyphs = []                                   # (columns image or None, left, advance) per code
    for c in range(256):
        ch = bytes([c]).decode('mac_roman', 'replace') if c >= 32 and c != 127 else ' '
        adv = int(round(font.getlength(ch)))
        if c != 32 and font.getmask(ch).getbbox() is None:     # the font has no such glyph
            ch, adv = ' ', int(round(font.getlength(' ')))
        im = Image.new('1', (adv + 2 * PAD + 8, height), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = '1'
        d.text((PAD, dy), ch, font=font, fill=1, anchor='la')
        box = im.getbbox()
        if box is None:
            glyphs.append((None, 0, adv))
        else:
            glyphs.append((im.crop((box[0], 0, box[2], height)), box[0] - PAD, adv))
    W = sum(g[0].width for g in glyphs if g[0] is not None) or 1
    strip = Image.new('P', (W, height), 0)
    lines = ['FONT %d %d %d' % (asc, desc, height)]
    x = 0
    for g, left, adv in glyphs:
        if g is None:
            lines.append('%d 0 0 %d' % (x, adv))
            continue
        strip.paste(g.convert('L').point(lambda v: 1 if v else 0), (x, 0))     # ink is index 1
        lines.append('%d %d %d %d' % (x, g.width, left, adv))
        x += g.width
    strip.putpalette([0, 0, 0, 255, 255, 255] + [0] * 762)
    strip.save(stem + '.bmp', format='BMP')
    open(stem + '.fnt', 'w').write('\n'.join(lines) + '\n')
    return W, height, asc


# ----------------------------------------------------------------------------- sprite sheets
# Every sheet is a horizontal strip: frame n (1-based) sits at x = 1 + (n-1)*(w+1), y = 1.
# The backdrop each frame is drawn on - the colour the game builds its mask from - is white in all of
# these sheets (it fills the frame corners); the pure red around the strip is just the surround.
# (id, frames, w, h, name) - frames as passed to CompileSprite in [LoadApeironSprites]
SPRITES = [
    (1000, 52, 16, 16, 'shroom'),   (1001, 62, 16, 16, 'pede'),      (1002, 33, 16, 16, 'shooter'),
    (1003,  1,  4, 12, 'shot'),     (1004,  8, 16, 16, 'flea'),      (1005, 18, 42, 20, 'gecko'),
    (1006, 20, 24, 24, 'scorpion'), (1007, 12,  4,  4, 'scum'),      (1008, 10,  8,  8, 'points'),
    (1009, 19, 16, 16, 'explode_shooter'), (1010, 102, 8, 8, 'flak'), (1011, 16, 16, 16, 'splat'),
    (1012, 16, 16, 16, 'splat_red'), (1013, 28, 16, 16, 'coin'),     (1014,  9, 32, 32, 'scorpsplat'),
    (1015,  4, 14, 14, 'yummie'),   (1016,  1,  4, 12, 'ricochet'),  (1017, 40, 20, 20, 'fodder'),
    (1018, 10,  8,  8, 'bonuspoints'), (1019, 6, 16, 16, 'shield'),  (1020,  9, 32, 32, 'geckosplat'),
    (1100, 11, 24, 32, 'numbers'),      # [CompileNumbers]: the score/wave digits, stride 25
    (1101,  1, 16, 16, 'life'),         # [DrawLives] lights this for each shooter in hand
    # [UpdateWeapons] draws these in the red box on the score panel: the empty slot, then the five
    # power-ups the Yummiary page describes.
    (1102,  1, 16, 16, 'wpn_none'),     (1103,  1, 14, 14, 'wpn_curve'),
    (1104,  1, 14, 14, 'wpn_freemove'), (1105,  1, 14, 14, 'wpn_machinegun'),
    (1106,  1, 14, 14, 'wpn_shield'),   (1107,  1, 14, 14, 'wpn_lock'),
]
SHOT_OFFSET = (7, 3)    # PICT 1003/1016 put their single 4x12 frame at this offset

def extract_sprite(d, frames, fw, fh, palette, stem, origin=(1, 1)):
    """Write <stem>.bmp (the frames re-indexed to `palette`, packed as one strip with no gaps) and
    <stem>.spr ('SPRITE <frames> <w> <h> <transparent index>')."""
    try:
        im = pict.decode_indexed(d)
        ctab = {k: (c[0], c[1], c[2]) for k, c in im['ctab'].items()}
        remap, _, _ = remap_to_palette(ctab, palette)
    except ValueError:
        # the Simpsons and Muppets sheets are direct-colour PICTs rather than indexed ones, so they
        # are decoded to RGB and then mapped to the game's table the way QuickDraw would
        rgb = pict.decode(d).convert('RGB')
        cols = sorted(set(rgb.getdata()))
        ctab = {i: c for i, c in enumerate(cols)}
        remap, _, _ = remap_to_palette(ctab, palette)
        back = {c: i for i, c in ctab.items()}
        im = {'w': rgb.width, 'h': rgb.height,
              'indices': [back[p] for p in rgb.getdata()]}
    trans = next((i for i, c in enumerate(palette) if c == (255, 255, 255)), 0)
    ox, oy = origin
    # Most sheets are drawn with a one-pixel rule between frames and a one-pixel margin, so the
    # first frame starts at (1,1) and the pitch is fw+1. A few - the weapon icons on the score
    # panel among them - are a bare frame with no margin at all, and reading those from (1,1)
    # shaves a row and a column off, which is what left them clipped and sitting high.
    pitch = fw + 1
    if im['w'] == fw and im['h'] == frames * fh:
        ox, oy, pitch = 0, 0, fw
    n = min(frames, max(1, (im['w'] - ox + (pitch - fw)) // pitch))
    out = bytearray(n * fw * fh)
    for f in range(n):
        sx = ox + f * pitch
        for y in range(fh):
            row = oy + y
            if row >= im['h']: continue
            for x in range(fw):
                if sx + x >= im['w']: continue
                out[f * fw * fh + y * fw + x] = remap[im['indices'][row * im['w'] + sx + x]]
    save_indexed(fw, n * fh, bytes(out), palette, stem + '.bmp')   # frames stacked vertically in the file
    open(stem + '.spr', 'w').write('SPRITE %d %d %d %d\n' % (n, fw, fh, trans))
    return n, fw, fh, trans

# ----------------------------------------------------------------------------- colour icons (cicn)
def parse_cicn(d):
    """'cicn' -> (w, h, indices, {index: rgb8}, mask bits). Layout: PixMap(50), mask BitMap(14),
    icon BitMap(14), iconData(4), mask bits, icon bits, colour table, pixel data."""
    pm_rowbytes = struct.unpack('>H', d[4:6])[0] & 0x3FFF
    top, left, bot, right = struct.unpack('>hhhh', d[6:14])
    pixsize = struct.unpack('>H', d[4 + 28:4 + 30])[0]
    ctoff = struct.unpack('>I', d[4 + 38:4 + 42])[0]
    mask_rb = struct.unpack('>H', d[54:56])[0]            # BitMap: baseAddr(4), rowBytes(2), bounds(8)
    mtop, mleft, mbot, mright = struct.unpack('>hhhh', d[56:64])
    icon_rb = struct.unpack('>H', d[68:70])[0]
    itop, ileft, ibot, iright = struct.unpack('>hhhh', d[70:78])
    w, h = right - left, bot - top
    p = 82
    mask = d[p:p + mask_rb * (mbot - mtop)]; p += mask_rb * (mbot - mtop)
    p += icon_rb * (ibot - itop)
    seed, flags, n = struct.unpack('>IHH', d[p:p + 8]); p += 8
    ct = {}
    for i in range(n + 1):
        v, r, g, b = struct.unpack('>HHHH', d[p:p + 8]); p += 8
        ct[i if flags & 0x8000 else v] = (r >> 8, g >> 8, b >> 8)
    px = bytearray(w * h)
    for y in range(h):
        row = d[p + y * pm_rowbytes: p + (y + 1) * pm_rowbytes]
        for x in range(w):
            if pixsize == 8:
                px[y * w + x] = row[x] if x < len(row) else 0
            else:
                per = 8 // pixsize; m = (1 << pixsize) - 1
                byte = row[x // per] if x // per < len(row) else 0
                px[y * w + x] = (byte >> (8 - pixsize * (x % per + 1))) & m
    return w, h, bytes(px), ct, mask, mask_rb

def cicn_native_bmp(d, path):
    """Write the icon with its OWN colour table, so the port can map it to whatever table is on the
    monitor when it is drawn. cicn 128 goes up over the high-score screen, whose palette is not the
    game's, and re-indexing it to the game's table at extract time is what turned it the wrong
    colours. Pixels its mask clears become index 0, which is written as pure white."""
    w, h, px, ct, mask, mrb = parse_cicn(d)
    pal = [(255, 255, 255)] * 256
    remap = {}
    nxt = 1
    for k, c in sorted(ct.items()):
        if nxt > 255: break
        pal[nxt] = c; remap[k] = nxt; nxt += 1
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            bit = 1
            if mask and y * mrb + x // 8 < len(mask):
                bit = (mask[y * mrb + x // 8] >> (7 - x % 8)) & 1
            out[y * w + x] = remap.get(px[y * w + x], 0) if bit else 0
    save_indexed(w, h, bytes(out), pal, path)
    return w, h, nxt - 1

def cicn_bmp(d, palette, path):
    """Write the icon re-indexed to `palette`; pixels its mask clears become the palette's white."""
    w, h, px, ct, mask, mrb = parse_cicn(d)
    remap, _, _ = remap_to_palette(ct, palette)
    trans = next((i for i, c in enumerate(palette) if c == (255, 255, 255)), 0)
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            bit = 1
            if mask and y * mrb + x // 8 < len(mask):
                bit = (mask[y * mrb + x // 8] >> (7 - x % 8)) & 1
            out[y * w + x] = remap.get(px[y * w + x], trans) if bit else trans
    save_indexed(w, h, bytes(out), palette, path)
    return w, h, trans

# ----------------------------------------------------------------------------- QuickDraw text PICTs
# PICT 4000-4002 (the instruction pages) are drawing commands, not pictures: Geneva text runs with
# colour and rectangle fills. They are flattened here into a draw list the port replays.
def parse_draw_pict(d, palette):
    FIX = {0x00:0,0x02:8,0x03:2,0x04:2,0x05:2,0x06:4,0x07:4,0x08:2,0x09:8,0x0A:8,0x0B:4,0x0C:4,0x0D:2,
           0x0E:4,0x0F:4,0x10:8,0x11:1,0x15:2,0x16:2,0x17:0,0x18:0,0x19:0,0x1A:6,0x1B:6,0x1C:0,0x1D:6,
           0x1E:0,0x1F:6,0x20:8,0x21:4,0x22:6,0x23:2,0x30:8,0x31:8,0x32:8,0x33:8,0x34:8,0x38:0,0x39:0,
           0x3A:0,0x3B:0,0x3C:0,0x40:8,0x41:8,0x42:8,0x43:8,0x44:8,0x48:0,0x49:0,0x4A:0,0x4B:0,0x4C:0,
           0x50:8,0x51:8,0x52:8,0x53:8,0x54:8,0x58:0,0x59:0,0x5A:0,0x5B:0,0x5C:0,0x60:8,0x61:8,0x62:8,
           0x63:8,0x64:8,0x68:0,0x69:0,0x6A:0,0x6B:0,0x6C:0,0xA0:2}
    def near(rgb):
        return min(range(256), key=lambda j: sum((a - b) ** 2 for a, b in zip(palette[j], rgb)))
    pos = 14 if d[10:14] == b'\x00\x11\x02\xff' else 10
    x = y = 0
    font, size, face, col = 3, 12, 0, (0, 0, 0)
    last_rect = (0, 0, 0, 0)
    out, bitmaps = [], []
    while pos + 2 <= len(d):
        op = struct.unpack('>H', d[pos:pos + 2])[0]; pos += 2
        if op == 0xFF: break
        if op == 0x0C00: pos += 24
        elif op == 0x01: pos += struct.unpack('>H', d[pos:pos + 2])[0]
        elif op == 0xA1: pos += 2 + 2 + struct.unpack('>H', d[pos + 2:pos + 4])[0]
        elif op in (0x2C, 0x2D, 0x2E, 0x2F): pos += 2 + struct.unpack('>H', d[pos:pos + 2])[0]
        elif op == 0x03: font = struct.unpack('>H', d[pos:pos + 2])[0]; pos += 2
        elif op == 0x0D: size = struct.unpack('>H', d[pos:pos + 2])[0]; pos += 2
        elif op == 0x04: face = d[pos]; pos += 2
        elif op == 0x1A:
            r, g, b = struct.unpack('>HHH', d[pos:pos + 6]); col = (r >> 8, g >> 8, b >> 8); pos += 6
        elif op == 0x20:                                    # Line (the red rule under a page title)
            y1, x1, y2, x2 = struct.unpack('>hhhh', d[pos:pos + 8]); pos += 8
            out.append(('L', x1, y1, x2, y2, near(col)))
        elif op in (0x28, 0x29, 0x2A, 0x2B):
            if op == 0x28: y, x = struct.unpack('>hh', d[pos:pos + 4]); pos += 4
            elif op == 0x29: x += d[pos]; pos += 1
            elif op == 0x2A: y += d[pos]; pos += 1
            else: x += d[pos]; y += d[pos + 1]; pos += 2
            n = d[pos]; txt = d[pos + 1:pos + 1 + n]; pos += 1 + n
            out.append(('T', x, y, font, size, face, near(col), txt))
        elif op in (0x30, 0x31, 0x32, 0x33, 0x34):          # frame/paint/erase/invert/fill rect
            t_, l_, b_, r_ = struct.unpack('>hhhh', d[pos:pos + 8]); pos += 8
            last_rect = (l_, t_, r_, b_)
            out.append(('R' if op == 0x31 else 'F', l_, t_, r_, b_, near(col)))
        elif op in (0x38, 0x39, 0x3A, 0x3B, 0x3C):
            out.append(('R' if op == 0x39 else 'F', *last_rect, near(col)))
        elif op in (0x90, 0x98, 0x99, 0x9A, 0x9B, 0x91):    # an icon drawn inline with the text
            pict._CAPTURE = []
            try:
                newpos, _img, dst = pict.draw(d, pos, op)
                cap = pict._CAPTURE[0]
                bitmaps.append((cap, dst))
                out.append(('B', dst[1], dst[0], len(bitmaps) - 1, dst[3] - dst[1], dst[2] - dst[0]))
                pos = newpos
            except Exception:
                break
            finally:
                pict._CAPTURE = None
        elif op in FIX: pos += FIX[op]
        else: break
        if pos & 1: pos += 1                                 # data is padded to keep opcodes aligned
    return out, bitmaps

def write_draw_pict(d, palette, stem, origin=(0, 0)):
    out, bitmaps = parse_draw_pict(d, palette)
    if origin != (0, 0):          # the picture's own frame is not at 0,0; shift it to the item rect
        ox, oy = origin
        shifted = []
        for e in out:
            if e[0] in ('R', 'F', 'L'):      # rects and lines carry a second point to shift too
                shifted.append((e[0], e[1] - ox, e[2] - oy, e[3] - ox, e[4] - oy, e[5]))
            else:
                shifted.append((e[0], e[1] - ox, e[2] - oy) + e[3:])
        out = shifted
    lines = []
    for e in out:
        if e[0] == 'T':
            _, x, y, font, size, face, c, txt = e
            lines.append(b'T %d %d %d %d %d %d ' % (x, y, font, size, face, c) + txt)
        else:
            lines.append(('%s %d %d %d %d %d' % e).encode())   # R/F: rect+colour, B: x y index 0 0
    open(stem + '.txt', 'wb').write(b'\n'.join(lines) + b'\n')
    for i, (cap, _dst) in enumerate(bitmaps):
        w, h, ps, rows = cap['w'], cap['h'], cap['pixsize'], cap['rows']
        idx = bytearray()
        for row in rows:
            if ps == 8:
                idx += row[:w]
            else:
                per = 8 // ps; m = (1 << ps) - 1
                idx += bytes((row[x // per] >> (8 - ps * (x % per + 1))) & m for x in range(w))
        ctab = {k: (c[0], c[1], c[2]) for k, c in (cap['ctab'] or {}).items()}
        remap, _, _ = remap_to_palette(ctab, palette)
        save_indexed(w, h, bytes(remap.get(b, 0) for b in idx), palette, '%s_img%d.bmp' % (stem, i))
    return len(out), len(bitmaps)

# ----------------------------------------------------------------------------- colour cursor (crsr)
def write_cursor(d, path):
    """'crsr' 128 - the pointing hand [LoadApeironSprites] loads and [main]/[AboutWindowUpdate] set.

    It is written as a small text file rather than a BMP because the cursor is drawn over both of the
    game's colour tables: the port resolves its colours against whichever one is up.
    Layout: crsrType(2) crsrMap(4) crsrData(4) crsrXData(4) crsrXValid(2) crsrXHandle(4)
            crsr1Data(32) crsrMask(32) crsrHotSpot(4) ..."""
    pmap, pdata = struct.unpack('>II', d[2:10])
    mask = d[52:84]
    hy, hx = struct.unpack('>hh', d[84:88])
    rb = struct.unpack('>H', d[pmap + 4:pmap + 6])[0] & 0x3FFF
    top, left, bot, right = struct.unpack('>hhhh', d[pmap + 6:pmap + 14])
    pixsize = struct.unpack('>H', d[pmap + 32:pmap + 34])[0]
    ctoff = struct.unpack('>I', d[pmap + 42:pmap + 46])[0]
    seed, fl, n = struct.unpack('>IHH', d[ctoff:ctoff + 8])
    ct = {}
    for i in range(n + 1):
        v, r, g, b = struct.unpack('>HHHH', d[ctoff + 8 + 8*i: ctoff + 16 + 8*i])
        ct[i if fl & 0x8000 else v] = (r >> 8, g >> 8, b >> 8)
    w, h = right - left, bot - top
    keys = sorted(ct)
    lines = ['CURSOR %d %d %d %d' % (w, h, hx, hy), 'COLORS %d' % len(keys)]
    lines += ['%d %d %d' % ct[k] for k in keys]
    per = 8 // pixsize; m = (1 << pixsize) - 1
    for y in range(h):
        row = d[pdata + y*rb: pdata + (y+1)*rb]
        mrow = struct.unpack('>H', mask[y*2:y*2+2])[0]
        s = ''
        for x in range(w):
            v = (row[x // per] >> (8 - pixsize * (x % per + 1))) & m
            s += chr(ord('a') + keys.index(v)) if (mrow >> (15 - x)) & 1 else '.'
        lines.append(s)
    open(path, 'w').write('\n'.join(lines) + '\n')
    return w, h, hx, hy, len(keys)

# ----------------------------------------------------------------------------- sound / text data
def snd_to_wav(d, path):
    fmt = struct.unpack('>H', d[:2])[0]; p = 2
    if fmt == 1: p += 2 + 6 * struct.unpack('>H', d[p:p+2])[0]
    else: p += 2
    nc = struct.unpack('>H', d[p:p+2])[0]; p += 2; off = None
    for _ in range(nc):
        cmd, p1, p2 = struct.unpack('>HhI', d[p:p+8]); p += 8
        if (cmd & 0x7FFF) in (0x50, 0x51): off = p2
    h = d[off:off+22]
    n = struct.unpack('>I', h[4:8])[0]; rate = round(struct.unpack('>I', h[8:12])[0] / 65536)
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(1); w.setframerate(rate)
    w.writeframes(d[off+22:off+22+n]); w.close()

def write_strings(app, scores_res, path):
    """strings.txt: line N (1-based) = STR# 133 string N (Mac Roman bytes), then 32 = version, 33 = unregistered
    name, 34 = copies. (version / 'NOT REGISTERED' come from the app's data section.)"""
    d = app[('STR#', 133)]; c = struct.unpack('>H', d[:2])[0]; q = 2; out = []
    for _ in range(c):
        L = d[q]; out.append(d[q+1:q+1+L]); q += 1 + L
    out += [b'1.0.3', b'NOT REGISTERED', b'']
    open(path, 'wb').write(b'\n'.join(out) + b'\n')
    return len(out)

def write_scores(apPr, path):
    """scores.txt: 'volume N' / 'music N' (indexes into the value strings) then 10 lines 'name<TAB>score<TAB>wave'."""
    vol, = struct.unpack('>h', apPr[0x100:0x102]); mus, = struct.unpack('>h', apPr[0x104:0x106])
    out = [b'volume %d' % vol, b'music %d' % mus]
    for i in range(10):
        o = 0x106 + i*0x100; L = apPr[o]; name = apPr[o+1:o+1+L]
        sc, = struct.unpack('>I', apPr[0xB06+4*i:0xB0A+4*i]); lv, = struct.unpack('>h', apPr[0xB2E+2*i:0xB30+2*i])
        out.append(name + b'\t%d\t%d' % (sc, lv))
    open(path, 'wb').write(b'\n'.join(out) + b'\n')

# ----------------------------------------------------------------------------- main
# The three artwork/sound packs that ship alongside the game, each a folder of its own next to the
# application. Simpsons and Muppets are replacement sprite PICTs (the whole set and four of them);
# Star Trek is a replacement sound and music file. Each is written to assets/packs/<name>/ holding
# only what it replaces, so switching one on at runtime is a matter of looking there first.
PACKS = [
    ('simpsons', 'Simpson Apeiron \u0192/.rsrc/Apeiron Sprites', 'sprites'),
    ('muppets',  'Apeiron Muppets \u0192/.rsrc/Apeiron Muppets', 'sprites'),
    ('startrek', 'Star Trek Apeiron \u0192/Equipment/.rsrc/Apeiron Sounds', 'sounds'),
]

def find_pack(game, rel):
    """The folder names carry a Mac florin; unpackers spell it in several ways."""
    head, tail = rel.split('/', 1)
    base = head.rstrip(' \u0192')
    for name in sorted(os.listdir(game)):
        if name.startswith(base) and os.path.isdir(os.path.join(game, name)):
            p = os.path.join(game, name, tail)
            if os.path.exists(p): return p
    return None

def extract_packs(game, out, game_clut):
    for name, rel, kind in PACKS:
        src = find_pack(game, rel)
        if not src:
            print(f'  pack {name}: not present, skipped')
            continue
        d = os.path.join(out, 'packs', name)
        r = {(t, i): x for t, i, n, a, x in rsrc.parse(src)}
        if kind == 'sprites':
            os.makedirs(f'{d}/spr', exist_ok=True)
            done = []
            for pid, frames, fw, fh, stem in SPRITES:
                if ('PICT', pid) not in r: continue
                org = SHOT_OFFSET if pid in (1003, 1016) else (1, 1)
                extract_sprite(r[('PICT', pid)], frames, fw, fh, game_clut, f'{d}/spr/{stem}', org)
                done.append(stem)
            print(f'  pack {name}: {len(done)} sprite sheets ({", ".join(done[:6])}{"..." if len(done) > 6 else ""})')
        else:
            os.makedirs(f'{d}/snd', exist_ok=True)
            n = 0
            for (t, i), x in sorted(r.items()):
                if t != 'snd ': continue
                try: snd_to_wav(x, f'{d}/snd/{i}.wav'); n += 1
                except Exception: pass
            m = find_pack(game, rel.replace('Apeiron Sounds', 'Apeiron Music'))
            if m:
                mr = {(t, i): x for t, i, nm, a, x in rsrc.parse(m)}
                for i, stem in ((1000, 'music_start'), (1001, 'music_loop')):
                    if ('snd ', i) in mr:
                        try: snd_to_wav(mr[('snd ', i)], f'{d}/snd/{stem}.wav'); n += 1
                        except Exception: pass
            print(f'  pack {name}: {n} sounds')

def main(game, out, fonts):
    os.makedirs(out, exist_ok=True)
    res = lambda f: {(t, i): d for t, i, n, a, d in rsrc.parse(f)}
    gres = lambda f: res(os.path.join(game, '.rsrc', f))
    titles, app, sounds, scores = gres('Apeiron Titles'), gres('Apeiron'), gres('Apeiron Sounds'), gres('Apeiron Scores')
    game_clut, title_clut = clut(app[('clut', 128)]), clut(app[('clut', 129)])

    print('ambrosia_logo.bmp', indexed_bmp(titles[('PICT', 999)], clut(titles[('clut', 999)]), f'{out}/ambrosia_logo.bmp'))
    print('apeiron_intro.bmp', indexed_bmp(titles[('PICT', 889)], clut(titles[('clut', 889)]), f'{out}/apeiron_intro.bmp'))
    print('title_logo.bmp   ', indexed_bmp(titles[('PICT', 400)], title_clut, f'{out}/title_logo.bmp'))
    print('title_morrison.bmp', indexed_bmp(titles[('PICT', 402)], title_clut, f'{out}/title_morrison.bmp'))

    # patterns: 400 is drawn under the Title Clut; 128-138 (level backgrounds), 401, 402 under the Game Clut
    for pid in list(range(128, 139)) + [400, 401, 402]:
        pal = title_clut if pid == 400 else game_clut
        w, h, used, exact, worst = pattern_bmp(titles[('ppat', pid)], pal, f'{out}/pattern_{pid}.bmp')
        print(f'pattern_{pid}.bmp {w}x{h}  colours {used}  exact in {"Title" if pid == 400 else "Game"} Clut {exact}  worst nearest-colour error {worst:.0f}/441')

    os.makedirs(f'{out}/snd', exist_ok=True)
    nsnd = 0
    for (t, i), d in sorted(sounds.items()):
        if t != 'snd ': continue
        try: snd_to_wav(d, f'{out}/snd/{i}.wav'); nsnd += 1
        except Exception as e: print('  snd %d failed: %s' % (i, e))
    music = gres('Apeiron Music')
    for i, name in ((1000, 'music_start'), (1001, 'music_loop')):
        try: snd_to_wav(music[('snd ', i)], f'{out}/snd/{name}.wav'); nsnd += 1
        except Exception as e: print('  %s failed: %s' % (name, e))
    print('sounds written:', nsnd)

    os.makedirs(f'{out}/spr', exist_ok=True)
    sprites = gres('Apeiron Sprites')
    for pid, frames, fw, fh, name in SPRITES:
        org = SHOT_OFFSET if pid in (1003, 1016) else (1, 1)
        n, w, h, tr = extract_sprite(sprites[('PICT', pid)], frames, fw, fh, game_clut, f'{out}/spr/{name}', org)
        print(f'  spr/{name}: {n} frames of {w}x{h}, transparent index {tr}')

    # fonts. New York 14/18/24 are the game's own embedded bitmaps (they differ from the system's for 18 and 24)
    ny = {14: 1067, 18: 32121, 24: 28514}
    for size, nid in ny.items():
        print(f'font_newyork{size}', nfnt_to_font(app[('NFNT', nid)], f'{out}/font_newyork{size}'))
    for slot, nfnt_id, stem, real, asc, desc in (('Geneva', 3130, 'font_geneva9', 'Geneva', 10, 2),
                                                 ('Chicago', 1025, 'font_chicago12', 'Chicago', 12, 3)):
        if real in fonts:
            r = res(fonts[real])
            print(stem, nfnt_to_font(r[('NFNT', nfnt_id)], f'{out}/{stem}'))
        elif 'ttf_' + slot in fonts:
            print(stem, '(%s)' % os.path.basename(fonts['ttf_' + slot]),
                  ttf_to_font(fonts['ttf_' + slot], f'{out}/{stem}', asc, desc))
        else:
            print(stem, '(stand-in: New York 14)', nfnt_to_font(app[('NFNT', ny[14])], f'{out}/{stem}'))

    # the in-game score panel, the title screen's buttons and the three instruction pages
    print('scorematt.bmp   ', indexed_bmp(titles[('PICT', 401)], game_clut, f'{out}/scorematt.bmp'))
    for cid, name in ((1000, 'btn_up'), (1001, 'btn_up_hit'), (1002, 'btn_down'), (1003, 'btn_down_hit')):
        print(f'  {name}.bmp', cicn_bmp(app[('cicn', cid)], title_clut, f'{out}/{name}.bmp'))
    # cicn 128: the grinning face DITL 161 puts beside "Congratulations, you got a high score!!!"
    print('  dialog_icon.bmp', cicn_native_bmp(app[('cicn', 128)], f'{out}/dialog_icon.bmp'))
    for i in range(3):
        n, nb = write_draw_pict(titles[('PICT', 4000 + i)], game_clut, f'{out}/about_{i}')
        print(f'  about_{i}.txt: {n} draw ops, {nb} embedded picture(s)')

    # [PauseUpdate] draws PICT 3130 ("PAUSED - Hit the Caps Lock key to continue") over the blacked-out
    # game window, and [LoadApeironSprites] loads crsr 128, the pointing hand the title screen uses.
    print('paused.bmp       ', indexed_bmp(titles[('PICT', 3130)], game_clut, f'{out}/paused.bmp'))
    print('cursor_hand.cur  ', write_cursor(app[('crsr', 128)], f'{out}/cursor_hand.cur'))

    # DITL 150's item 6 is a userItem at (118,225)-(268,241) that [DoAbout] attaches a draw proc to:
    # the line after "thanks:" in the credits picture. STR# 150 is the list it works through.
    d = app[('STR#', 150)]; c = struct.unpack('>H', d[:2])[0]; q = 2; thanks = []
    for _ in range(c):
        n = d[q]; thanks.append(d[q + 1:q + 1 + n].decode('mac_roman')); q += 1 + n
    open(f'{out}/thanks.txt', 'w', encoding='mac_roman').write('\n'.join(thanks) + '\n')
    print('thanks.txt       ', len(thanks), 'lines')

    print('asset packs:')
    extract_packs(game, out, game_clut)

    # [GameOver]: PICT 888 under clut 888, then the eight intro-animation frames over it
    print('gameover_logo.bmp', indexed_bmp(titles[('PICT', 888)], clut(titles[('clut', 888)]), f'{out}/gameover_logo.bmp'))
    for i in range(8):
        indexed_bmp(titles[('PICT', 900 + i)], clut(titles[('clut', 888)]), f'{out}/gameover_anim{i}.bmp')
    print('  gameover_anim0..7.bmp')
    print('ending.bmp       ', indexed_bmp(app[('PICT', 667)], clut(titles[('clut', 888)]), f'{out}/ending.bmp'))

    # the About dialog's three pictures ([DoAbout] puts up DLOG 150) and the poem [ZappaTribute]
    # shows when x is pressed on the title screen (DLOG 162, PICT 5555, drawn text not a bitmap)
    print('about_logo.bmp   ', indexed_bmp(app[('PICT', 151)], title_clut, f'{out}/about_logo.bmp'))
    print('about_credits.bmp', indexed_bmp(app[('PICT', 150)], title_clut, f'{out}/about_credits.bmp'))
    print('about_strip.bmp  ', indexed_bmp(app[('PICT', 153)], title_clut, f'{out}/about_strip.bmp'))
    n, _ = write_draw_pict(app[('PICT', 5555)], title_clut, f'{out}/poem', (74 - 4, 48 - 5))
    print(f'  poem.txt: {n} draw ops')

    print('strings.txt lines:', write_strings(app, scores, f'{out}/strings.txt'))
    write_scores(scores[('ApPr', 128)], f'{out}/scores.txt')

# ------------------------------------------------------------------ the whole pipeline in one go
def macbinary_name(path):
    """(name, type, creator) if this file looks like MacBinary, else None."""
    try:
        with open(path, 'rb') as f:
            h = f.read(128)
    except (IsADirectoryError, PermissionError, FileNotFoundError):
        return None
    if len(h) < 128 or h[0] != 0 or h[74] != 0 or h[82] != 0 or not 1 <= h[1] <= 63:
        return None
    return (h[2:2 + h[1]].decode('mac_roman', 'replace'),
            h[65:69].decode('mac_roman', 'replace'),
            h[69:73].decode('mac_roman', 'replace'))


def _rmtree(path):
    """Remove a folder tree, including the read-only files Windows will not otherwise let go of."""
    def onerror(func, p, _exc):
        try:
            os.chmod(p, stat.S_IWRITE)
            func(p)
        except OSError:
            pass
    shutil.rmtree(path, onerror=onerror)


def unzip_game(zpath):
    """Unpack the game's zip into a fresh temporary folder and return (folder, folder-to-remove).

    ZipFile.extract already turns characters Windows cannot have in a file name into underscores -
    the Mac's 'Icon' file ends in a carriage return - but a name that still cannot be created is
    skipped with a warning rather than stopping everything: the files that matter are the ones in
    .rsrc/, and one stray file must not cost the whole run.
    """
    tmp = tempfile.mkdtemp(prefix='apeiron-zip-')
    skipped = 0
    try:
        with zipfile.ZipFile(zpath) as z:
            for m in z.infolist():
                try:
                    z.extract(m, tmp)
                except (OSError, ValueError) as e:
                    skipped += 1
                    print("  skipped %r (%s)" % (m.filename, e))
    except zipfile.BadZipFile:
        _rmtree(tmp)
        sys.exit("'%s' is not a zip file I can read" % zpath)
    if skipped:
        print('  %d entries could not be unpacked; carrying on with the rest' % skipped)
    # The game's folder is the shallowest one that has a .rsrc/ in it - the zip may hold it at the
    # top or inside a folder of its own.
    best = None
    for root, dirs, _files in os.walk(tmp):
        if '.rsrc' in dirs:
            best = root
            break
    if best is None:
        best = tmp          # no .rsrc/ anywhere: maybe it is a zip of MacBinary files
    print('unpacked %s' % zpath)
    return best, tmp


def prepare(src):
    """Whatever the user has, hand main() a folder in the layout it wants: the game's files beside
    a .rsrc/ directory holding their resource forks.

    A folder already in that shape is used as it stands. A single MacBinary file is split into both
    forks. A folder of MacBinary files - which is what a download of the game unpacks to - is split
    file by file into one working folder. A .zip is unpacked first and then treated as the folder.

    Returns (folder, [temporary folders to remove when finished]).
    """
    if os.path.isfile(src) and zipfile.is_zipfile(src):
        folder, tmp = unzip_game(src)
        got, more = prepare(folder)
        return got, [tmp] + more

    if os.path.isdir(src):
        if os.path.isdir(os.path.join(src, '.rsrc')):
            return src, []
        bins = [f for f in sorted(os.listdir(src)) if macbinary_name(os.path.join(src, f))]
        if not bins:
            sys.exit("'%s' has neither a .rsrc/ directory nor any MacBinary files in it" % src)
        work = tempfile.mkdtemp(prefix='apeiron-')
        out = os.path.join(work, 'game')
        os.makedirs(out, exist_ok=True)
        for f in bins:
            unpack_macbinary.split(os.path.join(src, f), out)
        print('unpacked %d MacBinary files' % len(bins))
        return out, [work]

    info = macbinary_name(src)
    if not info:
        sys.exit("'%s' is not a folder, a zip file or a MacBinary file" % src)
    if info[1:3] == ('APPL', 'VIS3'):
        sys.exit(
            "'%s' is the Installer VISE 3 installer, not the game itself.\n"
            "\n"
            "Its data fork is an archive in VISE's own format. The payload measures 7.94 bits per\n"
            "byte, so nothing in it is stored plainly, and the decompressor the installer carries\n"
            "(Dcmp 1005, 'Version 27 (PPC, decomp)') is itself packed rather than plain PowerPC\n"
            "code - so unpacking the archive means reverse engineering VISE's container first.\n"
            "That is a separate piece of work from this script.\n"
            "\n"
            "Run the installer once under SheepShaver or Basilisk II and point this script at the\n"
            "folder it installs, or at a folder of MacBinary files." % src)
    work = tempfile.mkdtemp(prefix='apeiron-')
    out = os.path.join(work, 'game')
    os.makedirs(out, exist_ok=True)
    name = unpack_macbinary.split(src, out)
    print('unpacked %s' % name)
    return out, [work]


# ------------------------------------------------------------------------------------- the fonts
# Geneva and Chicago are the two Mac system fonts the game draws its dialogs in. They are not in the
# game's own files (New York is, which is why it is not on this list) and they are Apple's, so they
# are not in this repository either: they come from the font suitcases of a Mac - the Geneva.zip and
# Chicago.zip that a Mac archive of the fonts produces, each holding .rsrc/<name>.
FONT_NAMES = ('Geneva', 'Chicago')


def _font_from_zip(zpath, name, keep):
    """The resource fork of `name` out of a zip that has it as .rsrc/<name>, written to a file in
    the `keep` folder; None if this zip is not that."""
    try:
        with zipfile.ZipFile(zpath) as z:
            for m in z.infolist():
                parts = m.filename.replace('\\', '/').split('/')
                if len(parts) >= 2 and parts[-2] == '.rsrc' and parts[-1] == name and not m.is_dir():
                    dest = os.path.join(keep, name + '.rsrc')
                    with z.open(m) as src, open(dest, 'wb') as dst:
                        shutil.copyfileobj(src, dst)
                    return dest
    except (zipfile.BadZipFile, OSError):
        pass
    return None


def find_fonts(search_dirs, game_dir, keep):
    """Find Geneva and Chicago. Looked for, in each folder given and in the unpacked game itself:
    source_fonts/<name>.rsrc, <name>.rsrc, <name>/.rsrc/<name>, .rsrc/<name>, and any zip whose
    name has the font's name in it (1794536557284_Geneva.zip is what a download is called)."""
    found = {}
    for name in FONT_NAMES:
        cands = []
        for d in search_dirs:
            cands += [os.path.join(d, 'source_fonts', name + '.rsrc'), os.path.join(d, name + '.rsrc'),
                      os.path.join(d, name, '.rsrc', name), os.path.join(d, '.rsrc', name),
                      os.path.join(d, 'source_fonts', name, '.rsrc', name)]
        for c in cands:
            if os.path.isfile(c):
                found[name] = c
                break
        if name in found:
            continue
        for root, _dirs, files in os.walk(game_dir):              # fonts packed beside the game
            if os.path.basename(root) == '.rsrc' and name in files:
                found[name] = os.path.join(root, name)
                break
        if name in found:
            continue
        for d in search_dirs:
            try:
                zips = sorted(f for f in os.listdir(d)
                              if f.lower().endswith('.zip') and name.lower() in f.lower())
            except OSError:
                continue
            for f in zips:
                got = _font_from_zip(os.path.join(d, f), name, keep)
                if got:
                    found[name] = got
                    break
            if name in found:
                break
    # Failing the real font, a free pixel font that looks like it: a .ttf or .otf with the name in it
    # (pixChicago.ttf, or what a download of it is called). The Geneva stand-in may be called Geneve.
    ttf_dirs = search_dirs + [os.path.join(d, 'source_fonts') for d in search_dirs] + [os.path.join(HERE, 'fonts')]
    for slot, words in (('Chicago', ('chicago',)), ('Geneva', ('geneva', 'geneve'))):
        if slot in found:
            continue
        for d in ttf_dirs:
            try:
                ttfs = sorted(f for f in os.listdir(d)
                              if f.lower().endswith(('.ttf', '.otf')) and any(w in f.lower() for w in words))
            except OSError:
                continue
            if ttfs:
                found['ttf_' + slot] = os.path.join(d, ttfs[0])
                break
    missing = [n for n in FONT_NAMES if n not in found and ('ttf_' + n) not in found]
    if missing:
        print(
            "\n*** %s not found - using the game's own New York font in its place for now. ***\n"
            "The game works, but its menus and the high-score box will look a little different.\n"
            "For the real look, put %s (from a Mac; each holds .rsrc/<font name>) in the same\n"
            "folder as the game's zip, or in Downloads, and run this again.\n"
            % (' and '.join(missing), ' and '.join(n + '.zip' for n in missing)))
    for n in sorted(found):
        print('font file for %-12s %s' % (n, found[n]))
    return found


DEFAULT_OUT = 'apeiron_classic_assets'


def default_out_for(src):
    """The output folder when none is named: beside the input, so it is where you are looking."""
    parent = os.path.dirname(os.path.abspath(src.rstrip('/\\')))
    return os.path.join(parent, DEFAULT_OUT)


def _home_dirs():
    h = os.path.expanduser('~')
    return [os.path.join(h, d) for d in ('Downloads', 'Desktop', 'Documents')] + [h]


def _find_game_zip():
    """No argument given: look for the game's zip where people leave downloads."""
    for d in [os.getcwd(), os.path.join(HERE, '..'), HERE] + _home_dirs():
        try:
            names = sorted(f for f in os.listdir(d)
                           if f.lower().endswith('.zip') and f.lower().startswith('apeiron')
                           and 'font' not in f.lower())
        except OSError:
            continue
        for f in names:
            return os.path.join(d, f)
    return None


def _writable(folder):
    """Can a folder be made and written to there?"""
    try:
        os.makedirs(folder, exist_ok=True)
        probe = os.path.join(folder, '.write-test')
        with open(probe, 'w') as fh:
            fh.write('x')
        os.remove(probe)
        return True
    except OSError:
        return False


def _pause():
    """Started by double-click or drag-and-drop on Windows the window closes the moment the script
    ends; wait so the message can be read."""
    if os.name == 'nt' and sys.stdin and sys.stdin.isatty() and len(sys.argv) < 3:
        try:
            input('\nPress Enter to close this window...')
        except (EOFError, KeyboardInterrupt):
            pass


def run():
    if len(sys.argv) > 3:
        sys.exit('usage: extract_assets.py [the game\'s .zip | its folder | a MacBinary file] [out_dir]\n'
                 '       out_dir defaults to "%s" beside the input' % DEFAULT_OUT)
    src = sys.argv[1] if len(sys.argv) > 1 else _find_game_zip()
    if not src:
        sys.exit("Give the game's zip as the argument (or drop it on this script), or leave\n"
                 "Apeiron*.zip in Downloads or in the folder you run this from.")
    src = src.strip('"')
    if not os.path.exists(src):
        sys.exit("'%s' does not exist" % src)
    print('game: %s' % os.path.abspath(src))
    out = sys.argv[2] if len(sys.argv) == 3 else default_out_for(src)
    if len(sys.argv) < 3 and not _writable(out):
        alt = os.path.join(os.path.expanduser('~'), 'Downloads', DEFAULT_OUT)
        if not _writable(alt):
            alt = os.path.join(os.path.expanduser('~'), DEFAULT_OUT)
        print("cannot write beside the input, using %s instead" % alt)
        out = alt
    convert(src, out)
    print('Put that folder in Download on the TV, or beside apeiron.exe on Windows.')


def convert(src, out):
    """The whole job: unpack, find fonts, convert, clean up. Prints as it goes and calls sys.exit
    with a message when it cannot go on, which is what both the command line and the window use."""
    game, temps = prepare(src)
    try:
        here = os.path.dirname(os.path.abspath(src.rstrip('/\\')))
        keep = tempfile.mkdtemp(prefix='apeiron-fonts-')
        temps.append(keep)
        fonts = find_fonts([here, HERE, os.path.join(HERE, '..'), os.getcwd()] + _home_dirs(), game, keep)
        main(game, out, fonts)
    except PermissionError as e:
        sys.exit("\nPermission denied: %s\n"
                 "Either this file or folder is open in another program, or the output folder (%s) is\n"
                 "somewhere this account may not write. Pass a different out_dir as the second argument."
                 % (e.filename or e, out))
    finally:
        for t in temps:
            _rmtree(t)
    print('\nDone: %s' % os.path.abspath(out))
    return out


if __name__ == '__main__':
    try:
        run()
    finally:
        _pause()
