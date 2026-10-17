"""before.py EXE RVA... : disassembla desde o inicio da funcao (ou do padding cc anterior) ate RVA+6"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); md = Cs(CS_ARCH_X86, CS_MODE_64)
for a in sys.argv[2:]:
    r = int(a, 16); f = pe.func(r)
    s = f[0] if f else r
    if not f:
        while pe.read(s - 1, 1) != b'\xcc' and r - s < 0x200: s -= 1
    print(f'== {r:#x} (desde {s:#x})')
    for ad, sz, m, op in md.disasm_lite(pe.read(s, r + 6 - s), s):
        print(f'  {ad:#09x} {m} {op}')
