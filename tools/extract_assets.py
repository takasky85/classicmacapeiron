#!/usr/bin/env python3
"""Extract the assets the Apeiron startup/title sequence needs, in SDL3-friendly formats.

usage: extract_assets.py <game_dir> <out_dir>
  game_dir = the unzipped Apeiron folder (holds 'Apeiron Titles', 'Apeiron Scores', ... and the hidden
             .rsrc/ folder with their resource forks)
Needs Pillow (pip install pillow). Fonts come from source_fonts/ (resource forks of the Mac font suitcases)
and from the game's own embedded New York bitmaps.
"""
import os, struct, sys, wave, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rsrc, pict
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
    ctab = {k: (c[0], c[1], c[2]) for k, c in im['ctab'].items()}
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
    im = pict.decode_indexed(d)
    ctab = {k: (c[0], c[1], c[2]) for k, c in im['ctab'].items()}
    remap, _, _ = remap_to_palette(ctab, palette)
    trans = next((i for i, c in enumerate(palette) if c == (255, 255, 255)), 0)
    ox, oy = origin
    n = min(frames, max(1, (im['w'] - ox) // (fw + 1)))
    out = bytearray(n * fw * fh)
    for f in range(n):
        sx = ox + f * (fw + 1)
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

def write_draw_pict(d, palette, stem):
    out, bitmaps = parse_draw_pict(d, palette)
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
def main(game, out):
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
    gen = res(os.path.join(HERE, '..', 'source_fonts', 'Geneva.rsrc'))
    chi = res(os.path.join(HERE, '..', 'source_fonts', 'Chicago.rsrc'))
    print('font_geneva9 ', nfnt_to_font(gen[('NFNT', 3130)], f'{out}/font_geneva9'))
    print('font_chicago12', nfnt_to_font(chi[('NFNT', 1025)], f'{out}/font_chicago12'))

    # the in-game score panel, the title screen's buttons and the three instruction pages
    print('scorematt.bmp   ', indexed_bmp(titles[('PICT', 401)], game_clut, f'{out}/scorematt.bmp'))
    for cid, name in ((1000, 'btn_up'), (1001, 'btn_up_hit'), (1002, 'btn_down'), (1003, 'btn_down_hit')):
        print(f'  {name}.bmp', cicn_bmp(app[('cicn', cid)], title_clut, f'{out}/{name}.bmp'))
    for i in range(3):
        n, nb = write_draw_pict(titles[('PICT', 4000 + i)], game_clut, f'{out}/about_{i}')
        print(f'  about_{i}.txt: {n} draw ops, {nb} embedded picture(s)')

    # [GameOver]: PICT 888 under clut 888, then the eight intro-animation frames over it
    print('gameover_logo.bmp', indexed_bmp(titles[('PICT', 888)], clut(titles[('clut', 888)]), f'{out}/gameover_logo.bmp'))
    for i in range(8):
        indexed_bmp(titles[('PICT', 900 + i)], clut(titles[('clut', 888)]), f'{out}/gameover_anim{i}.bmp')
    print('  gameover_anim0..7.bmp')
    print('ending.bmp       ', indexed_bmp(app[('PICT', 667)], clut(titles[('clut', 888)]), f'{out}/ending.bmp'))

    print('strings.txt lines:', write_strings(app, scores, f'{out}/strings.txt'))
    write_scores(scores[('ApPr', 128)], f'{out}/scores.txt')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
