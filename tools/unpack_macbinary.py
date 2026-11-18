#!/usr/bin/env python3
"""Split a classic Mac MacBinary file - the .bin the installer ships as - into its two forks.

A Mac file is a data fork and a resource fork, and MacBinary wraps both into one stream with a
128-byte header so it can travel over something that only understands one. Unwrapping it is the
first step to anything else: the resource fork is where the artwork, sounds, dialogs and strings
live, and tools/extract_assets.py reads it directly.

    python3 tools/unpack_macbinary.py Apeiron_0.bin out/

writes out/<name> (the data fork) and out/.rsrc/<name> (the resource fork), which is the layout the
Mac emulators use for a shared folder and the one the rest of these tools expect. With -l it lists
the resources in the fork instead of writing anything.

If the data fork turns out to be a StuffIt archive - which an Ambrosia installer usually is - this
says so and stops: unstuffing it needs StuffIt Expander or `unar`, and the simplest route is to run
the installer under an emulator (SheepShaver or Basilisk II) and take the installed folder, which
is what the rest of this repository is built from.
"""
import os, struct, sys

def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def parse(path):
    d = open(path, 'rb').read()
    if len(d) < 128:
        raise SystemExit('%s: too short to be MacBinary' % path)
    if d[0] != 0 or d[74] != 0 or d[82] != 0:
        raise SystemExit('%s: not MacBinary (byte 0, 74 or 82 is not zero)' % path)
    nlen = d[1]
    if not 1 <= nlen <= 63:
        raise SystemExit('%s: not MacBinary (bad name length %d)' % (path, nlen))
    name = d[2:2 + nlen].decode('mac_roman')
    typ, creator = d[65:69].decode('mac_roman'), d[69:73].decode('mac_roman')
    dlen, rlen = struct.unpack('>II', d[83:91])
    version = d[122]                                   # 129 = MacBinary II
    ok = version >= 129 and struct.unpack('>H', d[124:126])[0] == crc16(d[:124])
    dstart = 128
    rstart = dstart + (dlen + 127) // 128 * 128
    if rstart + rlen > len(d):
        raise SystemExit('%s: truncated - the forks do not fit in the file' % path)
    return dict(name=name, type=typ, creator=creator, crc_ok=ok,
                data=d[dstart:dstart + dlen], rsrc=d[rstart:rstart + rlen])

def split(path, out):
    """Write both forks of one MacBinary file into `out`, in the .rsrc/ layout. Returns the name."""
    f = parse(path)
    os.makedirs(os.path.join(out, '.rsrc'), exist_ok=True)
    open(os.path.join(out, f['name']), 'wb').write(f['data'])
    open(os.path.join(out, '.rsrc', f['name']), 'wb').write(f['rsrc'])
    return f['name']


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    listing = '-l' in argv
    args = [a for a in argv[1:] if not a.startswith('-')]
    f = parse(args[0])
    print('%s  type %r creator %r  data %d bytes, resource %d bytes%s'
          % (f['name'], f['type'], f['creator'], len(f['data']), len(f['rsrc']),
             '' if f['crc_ok'] else '  (header CRC does not match - MacBinary I, probably)'))

    if f['data'][:4] in (b'StuffIt', b'SIT!', b'SITD') or f['data'][:4] == b'StuI':
        print('the data fork is a StuffIt archive: unpack it with `unar`, or run the installer\n'
              'under SheepShaver or Basilisk II and take the installed folder')
    if listing or len(args) < 2:
        if f['rsrc']:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import rsrc, tempfile
            tmp = os.path.join(tempfile.gettempdir(), 'mb.rsrc')
            open(tmp, 'wb').write(f['rsrc'])
            kinds = {}
            for t, i, n, a, x in rsrc.parse(tmp):
                kinds.setdefault(t, []).append(i)
            for t in sorted(kinds):
                ids = sorted(kinds[t])
                print('  %-6s %3d  %s%s' % (t, len(ids), ids[:12], ' ...' if len(ids) > 12 else ''))
        return

    out = args[1]
    os.makedirs(os.path.join(out, '.rsrc'), exist_ok=True)
    open(os.path.join(out, f['name']), 'wb').write(f['data'])
    open(os.path.join(out, '.rsrc', f['name']), 'wb').write(f['rsrc'])
    print('wrote %s/%s and %s/.rsrc/%s' % (out, f['name'], out, f['name']))

if __name__ == '__main__':
    main(sys.argv)
