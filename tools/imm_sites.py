"""imm_sites.py DLL hexdword [n] - instrucoes com o imediato/dado (ex.: deadbeef) e as n anteriores (capstone)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); v = struct.pack('<I', int(sys.argv[2], 16)); back = int(sys.argv[3]) if len(sys.argv) > 3 else 6
md = Cs(CS_ARCH_X86, CS_MODE_64)
i = pe.text.find(v)
while i >= 0:
    rva = pe.text_rva + i
    f = pe.func(rva)
    start = f[0] if f else rva - 0x40
    ins = [x for x in md.disasm_lite(pe.read(start, rva - start + 16), start)]
    k = next((j for j, x in enumerate(ins) if x[0] <= rva < x[0] + x[1]), None)
    print(f'== {rva:#x} func {f and hex(f[0])}')
    if k is not None:
        for a, s, m, op in ins[max(0, k - back):k + 2]: print(f'  {a:#x} {m} {op}')
    i = pe.text.find(v, i + 1)
