"""Apeiron.pef: unpack the container the way Ghidra's PEF loader does, so a pointer in the
data section can be followed to the code it names.

Ghidra puts the code section at 0x10000000 and the data section straight after it, 16-byte
aligned, which lands at 0x1001c580 - the TOC base. A sprite's routine pointer holds the
address of a transition vector, whose first word is the code address:

    0x1001c96c -> TVector 0x1001cf74 -> code 0x10001828 (BounceMoveProc)

Point APEIRON_PEF at the Apeiron application's data fork, then:  python3 pef.py 1001caf8
"""
import os, struct, sys

CODE_BASE = 0x10000000          # Ghidra's PEF loader puts section 0 here
def unpack_pattern(s):
    out = bytearray(); i = 0
    def var():
        nonlocal i
        v = 0
        while True:
            b = s[i]; i += 1
            v = (v << 7) | (b & 0x7f)
            if not (b & 0x80): return v
    while i < len(s):
        b = s[i]; i += 1
        op, cnt = b >> 5, b & 0x1f
        if cnt == 0: cnt = var()
        if op == 0:
            out += b'\0' * cnt
        elif op == 1:
            out += s[i:i+cnt]; i += cnt
        elif op == 2:
            rep = var(); blk = s[i:i+cnt]; i += cnt
            out += blk * (rep + 1)
        elif op == 3:                      # repeated block with custom parts
            custom = var(); rep = var()
            common = s[i:i+cnt]; i += cnt
            for _ in range(rep):
                out += common
                out += s[i:i+custom]; i += custom
            out += common
        elif op == 4:                      # repeated zero with custom parts
            custom = var(); rep = var()
            for _ in range(rep):
                out += b'\0' * cnt
                out += s[i:i+custom]; i += custom
            out += b'\0' * cnt
        else:
            raise ValueError('pattern opcode %d' % op)
    return bytes(out)

PEF_PATH = os.environ.get('APEIRON_PEF', 'Apeiron')   # the application's DATA fork
d = open(PEF_PATH, 'rb').read()
nsec = struct.unpack('>H', d[32:34])[0]
secs = []
for k in range(nsec):
    o = 40 + k*28
    _, defA, total, unp, pk, con = struct.unpack('>6i', d[o:o+24])
    secs.append((d[o+24], defA, total, unp, pk, con))
code = d[secs[0][5]: secs[0][5] + secs[0][4]]
DATA_BASE = CODE_BASE + ((len(code) + 0xf) & ~0xf)
data = unpack_pattern(d[secs[1][5]: secs[1][5] + secs[1][4]])
data += b'\0' * (secs[1][2] - len(data))
print('code %d bytes at %#x ; data %d bytes at %#x (unpacked %d)'
      % (len(code), CODE_BASE, len(data), DATA_BASE, secs[1][3]))

def dw(addr):
    off = addr - DATA_BASE
    return struct.unpack('>I', data[off:off+4])[0]

def follow(ptr_addr):
    """ptr -> TVector -> code address, the way Ghidra shows it after relocation."""
    tv = DATA_BASE + dw(ptr_addr)
    code_addr = CODE_BASE + dw(tv)
    return tv, code_addr

if __name__ == '__main__':
    for a in sys.argv[1:]:
        p = int(a, 16)
        tv, c = follow(p)
        print('%#x -> TVector %#x -> code %#x' % (p, tv, c))
