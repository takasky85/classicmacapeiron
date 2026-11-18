"""Annotated PowerPC disassembly of Apeiron.pef.

Resolves call targets to the names in the traceback tables, and TOC references (`addi rN, r2, off`)
to the globals the Ghidra decompile calls DAT_1001xxxx - including the block of sound handles
[HandleInitializations] loads, so `ST_SimplePlay` calls read as sound names.

    APEIRON_PEF=game/Apeiron python3 tools/dis.py GeckoMoveProc
    APEIRON_PEF=game/Apeiron python3 tools/dis.py 0x10007904
"""
import os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pef, pef_symbols

CODE_BASE, TOC = 0x10000000, 0x1001c580
SYMS = pef_symbols.build()
BY_NAME = {v: k for k, v in SYMS.items()}

SND_NAMES = {
    128:'Transition',129:'Hit',130:'Squish',131:'Shroom Pop',132:'Ship Explosion',133:'Ship Laser',
    134:'Yummie Appears',135:'Extra Ship',136:'Got Yummie',137:'Groucho Death',138:'Groucho Ouch',
    139:'Pause',140:'Groucho Laugh',141:'Munching 1',142:'Munching 2',143:'Intro Theme',
    144:'Level Clear',145:'Oh no',146:'Scorpian Dies',147:'Scorp Appears',148:'Missed It',
    149:'Diehl Belch',150:'Hooka Music',151:'Moan',152:'Killed Root',153:'Riff',154:'Mult Appears',
    155:'Ricochet',156:'myHooHa',157:'Boing 1',158:'Boing 2',159:'Bonk',160:'Splut',
    161:'Bonus Appears',162:'Explosion',163:'Shot Gecko',164:'Hey!',165:'Gecko Appears',
    166:'Got Funky',167:'Bam!',168:'Warning Will Robinson!',169:'Poisoned!',1000:'Music Start',
    1001:'Music Loop',
}

def sound_handles(decompile='b.c'):
    """DAT_1001dcXX = _ST_LoadSound(0xNN) in the Ghidra export -> {global address: snd id}."""
    out = {}
    try:
        text = open(decompile, encoding='utf-8', errors='replace').read()
    except OSError:
        return out
    for m in re.finditer(r'(DAT_1001[0-9a-f]{4})\s*=\s*_ST_LoadSound\((0x[0-9a-f]+|\d+)\)', text):
        out[int(m.group(1)[4:], 16)] = int(m.group(2), 0)
    return out

SOUNDS = sound_handles(os.environ.get('APEIRON_DECOMPILE', 'b.c'))

def _elf():
    """Wrap the raw code section in a minimal big-endian PPC32 ELF for llvm-objdump."""
    path = os.path.join(tempfile.gettempdir(), 'apeiron_code.elf')
    if os.path.exists(path) and os.path.getsize(path) == len(pef.code) + 192:
        return path
    code, shstr, eh, she, nsh = pef.code, b'\0.text\0.shstrtab\0', 52, 40, 3
    off_code = eh; off_str = off_code + len(code); off_sh = (off_str + len(shstr) + 3) & ~3
    hdr = struct.pack('>16sHHIIIIIHHHHHH', b'\x7fELF\x01\x02\x01' + b'\0' * 9,
                      1, 20, 1, 0, 0, off_sh, 0, eh, 0, 0, she, nsh, 2)
    sh = b'\0' * she
    sh += struct.pack('>10I', 1, 1, 6, CODE_BASE, off_code, len(code), 0, 0, 4, 0)
    sh += struct.pack('>10I', 7, 3, 0, 0, off_str, len(shstr), 0, 0, 1, 0)
    blob = bytearray(hdr + code + shstr)
    blob += b'\0' * (off_sh - len(blob))
    blob += sh
    open(path, 'wb').write(blob)
    return path

_LINES = None
def lines():
    global _LINES
    if _LINES is None:
        txt = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', _elf()],
                             capture_output=True, text=True).stdout
        _LINES = {}
        for ln in txt.splitlines():
            m = re.match(r'^\s*([0-9a-f]{8}):\s*(.*)$', ln)
            if m:
                _LINES[int(m.group(1), 16)] = m.group(2).strip().replace('\t', ' ')
    return _LINES

def word(addr):
    o = addr - CODE_BASE
    return struct.unpack('>I', pef.code[o:o + 4])[0]

def toc_name(off):
    a = TOC + off
    if a in SOUNDS:
        sid = SOUNDS[a]
        return 'DAT_%08x = snd %d "%s"' % (a, sid, SND_NAMES.get(sid, '?'))
    return 'DAT_%08x' % a

def annotate(addr, text):
    m = re.match(r'bl?a?\s+0x([0-9a-f]+)', text) or re.match(r'b[a-z]*\s+.*0x([0-9a-f]+)', text)
    if text.startswith('bl '):
        t = int(re.search(r'0x([0-9a-f]+)', text).group(1), 16)
        return '%-34s ; -> %s' % (text, SYMS.get(t, '%#x' % t))
    m = re.match(r'(addi|lwz|stw|lha|lhz|sth|lbz|stb)\s+(\d+),?\s*(?:(\d+)\((\d+)\)|(\d+),\s*(-?\d+))', text)
    # TOC-relative: "addi 3, 2, 5372"  or  "lwz 3, 5372(2)"
    m1 = re.match(r'addi\s+(\d+),\s*2,\s*(-?\d+)$', text)
    m2 = re.match(r'(lwz|lha|lhz|lbz|stw|sth|stb)\s+(\d+),\s*(-?\d+)\(2\)$', text)
    if m1:
        return '%-34s ; %s' % (text, toc_name(int(m1.group(2))))
    if m2:
        return '%-34s ; %s' % (text, toc_name(int(m2.group(3))))
    if re.match(r'b[a-z]+\s', text) and '0x' in text:
        t = int(re.search(r'0x([0-9a-f]+)', text).group(1), 16)
        if t in SYMS:
            return '%-34s ; -> %s' % (text, SYMS[t])
    return text

def func(start, limit=400):
    """Disassemble from `start` to the zero word that ends the function."""
    L, out = lines(), []
    a = start
    for _ in range(limit):
        if word(a) == 0:
            break
        out.append((a, annotate(a, L.get(a, '?'))))
        a += 4
    return out

def show(which):
    start = BY_NAME.get(which) if not which.startswith('0x') else int(which, 16)
    if start is None:
        cands = [n for n in BY_NAME if which.lower() in n.lower()]
        print('no such function; did you mean: %s' % ', '.join(sorted(cands)[:10]))
        return
    print('===== %s (%#x), %d instructions' % (SYMS.get(start, which), start, len(func(start))))
    for a, t in func(start):
        print('%08x  %s' % (a, t))

if __name__ == '__main__':
    for w in sys.argv[1:]:
        show(w)
