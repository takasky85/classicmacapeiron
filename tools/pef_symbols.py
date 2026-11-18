"""Function names out of the PowerPC traceback tables the compiler left after each function.
This is where Ghidra's `.debug::_Name` labels come from."""
import struct, re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pef

def build():
    code, names = pef.code, {}
    for i in range(0, len(code) - 16, 4):
        if code[i:i+4] != b'\0\0\0\0':
            continue
        p = i + 4
        if code[p] != 0:                       # version must be 0
            continue
        f1, f2 = code[p+2], code[p+3]
        has_tboff    = (f1 >> 5) & 1
        has_ctl      = (f1 >> 3) & 1
        int_hndl     = (f2 >> 7) & 1
        name_present = (f2 >> 6) & 1
        if not (has_tboff and name_present):
            continue
        fixedparms = code[p+6]
        floatparms = code[p+7] >> 1
        q = p + 8
        if fixedparms or floatparms: q += 4                     # parminfo
        tboff = struct.unpack('>I', code[q:q+4])[0]; q += 4
        if int_hndl: q += 4
        if has_ctl:
            n = struct.unpack('>I', code[q:q+4])[0]; q += 4 + 4*n
        ln = struct.unpack('>H', code[q:q+2])[0]; q += 2
        nm = code[q:q+ln]
        if not (0 < ln < 96) or not re.fullmatch(rb'[.A-Za-z_][A-Za-z0-9_.:$]*', nm):
            continue
        start = i - tboff                      # tb_offset is measured to the zero word that ends the code
        if 0 <= start < len(code):
            names[0x10000000 + start] = nm.decode().lstrip('.')
    return names

if __name__ == '__main__':
    n = build()
    print('# %d functions named from the traceback tables' % len(n))
    for a, v in sorted(n.items()):
        print('%#010x  %s' % (a, v))
