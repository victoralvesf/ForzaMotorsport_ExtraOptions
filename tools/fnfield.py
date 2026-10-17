"""fnfield.py EXE TARGET_RVA DISP... - para cada funcao que referencia TARGET_RVA (rip-relative), lista as
instrucoes que usam algum dos deslocamentos DISP (ex.: campos do CDynamicRenderSettings lidos via o gerenciador
global 0x25f7e00)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); t = pe.text; base = pe.text_rva
tg = int(sys.argv[2], 16); disps = {int(x, 16) for x in sys.argv[3:]}
md = Cs(CS_ARCH_X86, CS_MODE_64)
funcs = set()
for i in range(len(t) - 4):
    disp = struct.unpack_from('<i', t, i)[0]
    for tail in (0, 1, 4):
        if base + i + 4 + tail + disp == tg and i >= 2 and (t[i-1] & 0xc7) == 5:
            f = pe.func(base + i)
            if f: funcs.add(f)
for f0, f1 in sorted(funcs):
    hits = []
    for ins in md.disasm(pe.d[pe.r2o(f0):pe.r2o(f1)], f0):
        for m in re.finditer(r'\[r\w+ \+ (0x[0-9a-f]+)\]', ins.op_str):
            if int(m.group(1), 16) in disps: hits.append(f'    {ins.address:#x} {ins.mnemonic} {ins.op_str}')
    if hits: print(f'func {f0:#x}-{f1:#x}'); print('\n'.join(hits))
