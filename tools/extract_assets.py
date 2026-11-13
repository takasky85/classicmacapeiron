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

    for name, i in (('transition', 128), ('level_clear', 144), ('intro_theme', 143)):
        snd_to_wav(sounds[('snd ', i)], f'{out}/{name}.wav')

    # fonts. New York 14/18/24 are the game's own embedded bitmaps (they differ from the system's for 18 and 24)
    ny = {14: 1067, 18: 32121, 24: 28514}
    for size, nid in ny.items():
        print(f'font_newyork{size}', nfnt_to_font(app[('NFNT', nid)], f'{out}/font_newyork{size}'))
    gen = res(os.path.join(HERE, '..', 'source_fonts', 'Geneva.rsrc'))
    chi = res(os.path.join(HERE, '..', 'source_fonts', 'Chicago.rsrc'))
    print('font_geneva9 ', nfnt_to_font(gen[('NFNT', 3130)], f'{out}/font_geneva9'))
    print('font_chicago12', nfnt_to_font(chi[('NFNT', 1025)], f'{out}/font_chicago12'))

    print('strings.txt lines:', write_strings(app, scores, f'{out}/strings.txt'))
    write_scores(scores[('ApPr', 128)], f'{out}/scores.txt')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
