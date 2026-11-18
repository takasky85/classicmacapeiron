import struct
from PIL import Image

def unpack_row(d, pos, rowbytes, big, unit=1):
    if big:
        n = struct.unpack('>H', d[pos:pos+2])[0]; pos += 2
    else:
        n = d[pos]; pos += 1
    end = pos + n; out = bytearray()
    while pos < end:
        f = d[pos]; pos += 1
        if f < 128:
            k = (f+1)*unit; out += d[pos:pos+k]; pos += k
        else:
            k = 257-f; out += d[pos:pos+unit]*k; pos += unit
    return bytes(out), end

def rect(d, o):
    t, l, b, r = struct.unpack('>hhhh', d[o:o+8]); return t, l, b, r

def decode(d):
    frame = rect(d, 2)
    W = frame[3]-frame[1]; H = frame[2]-frame[0]
    canvas = Image.new('RGBA', (max(W, 1), max(H, 1)), (255, 255, 255, 0))
    pos = 10
    ver = 1
    if d[pos:pos+2] == b'\x00\x11' and d[pos+2:pos+4] == b'\x02\xff':
        ver = 2; pos += 4
    elif d[pos:pos+2] == b'\x11\x01':
        pos += 2
    drew = 0
    def op_read():
        nonlocal pos
        if ver == 2:
            if pos % 2 == 1 and False: pass
            v = struct.unpack('>H', d[pos:pos+2])[0]; pos += 2
        else:
            v = d[pos]; pos += 1
        return v
    fixed = {0x00:0,0x02:8,0x03:2,0x04:2,0x05:2,0x06:4,0x07:4,0x08:2,0x09:8,0x0A:8,0x0B:4,0x0C:4,0x0D:2,0x0E:4,0x0F:4,0x10:8,
             0x1A:6,0x1B:6,0x1C:0,0x1D:6,0x1E:0,0x1F:6,0x20:8,0x21:4,0x22:6,0x23:2,
             0x30:8,0x31:8,0x32:8,0x33:8,0x34:8,0x38:0,0x39:0,0x3A:0,0x3B:0,0x3C:0,
             0x40:8,0x41:8,0x42:8,0x43:8,0x44:8,0x48:0,0x49:0,0x4A:0,0x4B:0,0x4C:0,
             0x50:8,0x51:8,0x52:8,0x53:8,0x54:8,0x58:0,0x59:0,0x5A:0,0x5B:0,0x5C:0,
             0x60:8,0x61:8,0x62:8,0x63:8,0x64:8,0x68:0,0x69:0,0x6A:0,0x6B:0,0x6C:0,
             0x70:None,0x71:None,0x72:None,0x73:None,0x74:None,0x78:None,0x79:None,0x7A:None,0x7B:None,0x7C:None,
             0xA0:2,0x0C00:24}
    while pos + (2 if ver == 2 else 1) <= len(d):
        op = op_read()
        if op == 0xFF or op == 0x00FF: break
        if op in (0x01, 0x70,0x71,0x72,0x73,0x74,0x78,0x79,0x7A,0x7B,0x7C) and op != 0x0C00 or op in (0x91, 0x99, 0x9B):
            pass
        if 0x80 <= op <= 0x84:
            n = struct.unpack('>H', d[pos:pos+2])[0]; pos += n; continue
        if op == 0x01:
            n = struct.unpack('>H', d[pos:pos+2])[0]; pos += n; continue
        if op in (0x70,0x71,0x72,0x73,0x74,0x78,0x79,0x7A,0x7B,0x7C):
            n = struct.unpack('>H', d[pos:pos+2])[0]; pos += n; continue
        if op in (0x28,):
            pos += 4; n = d[pos]; pos += 1+n; continue
        if op in (0x29,0x2A,0x2B):
            pos += 1; n = d[pos]; pos += 1+n; continue
        if op == 0x2C:
            pos += 2; n = d[pos]; pos += 1+n; continue
        if op == 0xA1:
            n = struct.unpack('>H', d[pos+2:pos+4])[0]; pos += 4+n; continue
        if op in (0x90, 0x91, 0x98, 0x99, 0x9A, 0x9B):
            pos, img, dst = draw(d, pos, op)
            if img is not None:
                canvas.paste(img, (dst[1]-frame[1], dst[0]-frame[0])); drew += 1
            continue
        if op in (0x8200, 0x8201):
            raise ValueError('QuickTime PICT')
        if op in fixed and fixed[op] is not None:
            pos += fixed[op]; continue
        if 0x0100 <= op <= 0x7FFF:
            pos += (op >> 8)*2; continue
        # the reserved ranges, which the Simpsons sprite file uses: $8000-$80FF carry no data and
        # everything from $8100 up carries a four-byte length followed by that many bytes
        if 0x8000 <= op <= 0x80FF:
            continue
        if op >= 0x8100:
            pos += 4 + struct.unpack('>I', d[pos:pos+4])[0]; continue
        raise ValueError('unknown op 0x%04x at %d' % (op, pos))
    if not drew:
        raise ValueError('no bitmap ops (vector-only PICT)')
    return canvas

_CAPTURE = None

def draw(d, pos, op):
    direct = op in (0x9A, 0x9B)
    packed = op in (0x98, 0x99, 0x9A, 0x9B)
    region = op in (0x91, 0x99, 0x9B)
    if direct: pos += 4
    rb = struct.unpack('>H', d[pos:pos+2])[0]; pos += 2
    ispix = bool(rb & 0x8000); rowbytes = rb & 0x3FFF
    bounds = rect(d, pos); pos += 8
    pixsize = 1; packtype = 0; cmp = 1; ctab = None
    if ispix:
        pmv, packtype, packsize, hres, vres, ptype, pixsize, cmp, cmpsize, planebytes, pmtable, res = struct.unpack('>HHIIIHHHHIII', d[pos:pos+36]); pos += 36
        if not direct:
            seed, flags, n = struct.unpack('>IHH', d[pos:pos+8]); pos += 8
            ctab = {}
            for i in range(n+1):
                v, r, g, b = struct.unpack('>HHHH', d[pos:pos+8]); pos += 8
                idx = i if (flags & 0x8000) else v
                ctab[idx] = (r >> 8, g >> 8, b >> 8)
    src = rect(d, pos); pos += 8
    dst = rect(d, pos); pos += 8
    mode = struct.unpack('>H', d[pos:pos+2])[0]; pos += 2
    if region:
        n = struct.unpack('>H', d[pos:pos+2])[0]; pos += n
    h = bounds[2]-bounds[0]; w = bounds[3]-bounds[1]
    rows = []
    for y in range(h):
        if not packed or (rowbytes < 8) or packtype == 1:
            rows.append(d[pos:pos+rowbytes]); pos += rowbytes
        else:
            unit = 2 if (pixsize == 16 and packtype != 0 or (pixsize == 16)) else 1
            r, pos = unpack_row(d, pos, rowbytes, rowbytes > 250, unit)
            rows.append(r)
    if _CAPTURE is not None:
        _CAPTURE.append(dict(rows=rows, w=w, h=h, pixsize=pixsize, ctab=ctab, dst=dst, bounds=bounds))
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y, row in enumerate(rows):
        if pixsize in (1, 2, 4, 8):
            per = 8//pixsize; mask = (1 << pixsize)-1
            for x in range(w):
                byte = row[x//per] if x//per < len(row) else 0
                v = (byte >> (8 - pixsize*(x % per + 1))) & mask
                if ctab is not None:
                    c = ctab.get(v, (0, 0, 0))
                elif pixsize == 1:
                    c = (0, 0, 0) if v else (255, 255, 255)
                else:
                    c = (v*17, v*17, v*17)
                px[x, y] = c + (255,)
        elif pixsize == 16:
            for x in range(w):
                v = (row[2*x] << 8) | row[2*x+1]
                px[x, y] = (((v >> 10) & 31)*255//31, ((v >> 5) & 31)*255//31, (v & 31)*255//31, 255)
        elif pixsize == 32:
            if cmp == 3 or cmp == 4:
                off = 0 if cmp == 3 else w
                for x in range(w):
                    px[x, y] = (row[off+x], row[off+w+x], row[off+2*w+x], 255)
            else:
                for x in range(w):
                    px[x, y] = (row[4*x+1], row[4*x+2], row[4*x+3], 255)
    return pos, img, dst


def decode_indexed(d):
    """Return dict(w,h,indices(bytes, 8 bits/pixel), ctab{index:(r8,g8,b8)}, frame) for an indexed PICT."""
    global _CAPTURE
    _CAPTURE = []
    try:
        decode(d)
        cap = _CAPTURE[0]
    finally:
        _CAPTURE = None
    w, h, ps, rows = cap['w'], cap['h'], cap['pixsize'], cap['rows']
    out = bytearray()
    for row in rows:
        if ps == 8:
            out += row[:w]
        elif ps in (1, 2, 4):
            per = 8//ps; m = (1 << ps)-1
            out += bytes((row[x//per] >> (8-ps*(x % per+1))) & m for x in range(w))
        else:
            raise ValueError('not indexed (%d bpp)' % ps)
    return dict(w=w, h=h, indices=bytes(out), ctab=cap['ctab'], pixsize=ps)
