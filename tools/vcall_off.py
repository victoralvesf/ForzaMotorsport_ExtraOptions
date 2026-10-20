"""vcall_off.py EXE 0xOFF... - calls 'call qword ptr [reg + OFF]' in .text, with the containing function."""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1])
offs = {int(x, 16) for x in sys.argv[2:]}
md = Cs(CS_ARCH_X86, CS_MODE_64)
pat = re.compile(r'qword ptr \[r\w+ \+ 0x([0-9a-f]+)\]$')
for m in re.finditer(rb'\xff[\x90-\x97\x50-\x57]', pe.text):
    i = m.start()
    for ad, sz, mn, op in md.disasm_lite(pe.text[i:i + 7], pe.text_rva + i, 1):
        g = pat.match(op) if mn == 'call' else None
        if g and int(g.group(1), 16) in offs:
            f = pe.func(ad)
            print(f'{ad:#x} {op} (func {f and hex(f[0])})')
