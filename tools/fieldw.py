"""fieldw.py EXE DISP LO HI : instrucoes que escrevem/leem [reg+DISP] (memoria) entre os RVAs LO..HI (por funcao)"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); disp = int(sys.argv[2], 16); lo = int(sys.argv[3], 16); hi = int(sys.argv[4], 16)
md = Cs(CS_ARCH_X86, CS_MODE_64)
a = lo
while a < hi:
    f = pe.func(a)
    if not f: a += 1; continue
    for ad, sz, m, op in md.disasm_lite(pe.read(f[0], f[1] - f[0]), f[0]):
        if ('+ %#x]' % disp) in op and 'rip' not in op and 'rsp' not in op:
            print(f'{f[0]:#x} {ad:#x} {m} {op}')
    a = max(f[1], a + 1)
