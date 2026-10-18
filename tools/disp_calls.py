"""disp_calls.py EXE DISP... - lista os call qword ptr [reg + DISP] (vcall por deslocamento) no .text, com a funcao
e as 6 instrucoes antes (ex.: slot de vtable embutida do contexto do FM6: 0x3d0 + 8 * slot)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); disps = {int(x, 16) for x in sys.argv[2:]}
md = Cs(CS_ARCH_X86, CS_MODE_64)
t = pe.text; base = pe.text_rva
for d in sorted(disps):
    pat = re.compile(rb'\xff[\x90-\x97]' + d.to_bytes(4, 'little'))
    for m in pat.finditer(t):
        at = base + m.start() - (1 if t[m.start() - 1] in (0x41, 0x43) else 0)
        f = pe.func(at)
        lo = max(f[0], at - 0x30) if f else at - 0x30
        ins = [x for x in md.disasm_lite(pe.read(lo, at - lo + 8), lo)]
        k = next((i for i, x in enumerate(ins) if x[0] == at), None)
        if k is None: print(f'{d:#x} {at:#x} (desalinhado)'); continue
        ctx = '; '.join(f'{x[2]} {x[3]}' for x in ins[max(0, k - 6):k])
        print(f'{d:#x} {at:#x} func {f and hex(f[0])}: {ctx}')
