"""dis.py EXE rva [count] [back]  - disassemble with RIP targets resolved; marks rva"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); rva = int(sys.argv[2], 16)
cnt = int(sys.argv[3]) if len(sys.argv) > 3 else 40
back = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
f = pe.func(rva)
start = f[0] if f and rva - f[0] <= back else rva - back if back else rva
md = Cs(CS_ARCH_X86, CS_MODE_64)
print(f'# func {f and hex(f[0])}-{f and hex(f[1])}')
n = 0
for a, s, m, op in md.disasm_lite(pe.read(start, 0x2000), start):
    if a + s <= rva - back and f and a < rva - back: continue
    b = pe.read(a, s).hex()
    mark = '>>' if a <= rva < a + s else '  '
    print(f'{mark}{a:#09x} {b:<22} {m} {op}')
    n += 1
    if n >= cnt: break
