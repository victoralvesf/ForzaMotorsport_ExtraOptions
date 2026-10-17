"""dcalls.py EXE lo hi [--sites disp] - histograma de 'call [reg+disp]' SEM carga de vtable antes (tabela dentro do objeto)"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_REG
from capstone.x86 import X86_REG_RIP, X86_REG_RSP
pe = PE(sys.argv[1]); lo = int(sys.argv[2], 16); hi = int(sys.argv[3], 16)
want = int(sys.argv[5], 16) if len(sys.argv) > 5 else None
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
hist = collections.Counter(); nf = collections.defaultdict(set)
for b, e in pe.funcs:
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    vt = set()
    for ins in md.disasm(pe.read(b, e - b), b):
        ops = ins.operands
        if ins.mnemonic == 'call' and len(ops) == 1 and ops[0].type == CS_OP_MEM:
            m = ops[0].mem
            if m.base not in vt and m.base not in (X86_REG_RIP, X86_REG_RSP, 0) and m.index == 0 and lo <= m.disp < hi:
                hist[m.disp] += 1; nf[m.disp].add(b)
                if want == m.disp: print(f'{b:#x} {ins.address:#x} {ins.op_str}')
            vt.clear(); continue
        if ins.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == CS_OP_REG:
            r = ops[0].reg; vt.discard(r)
            if ops[1].type == CS_OP_MEM and ops[1].mem.disp == 0 and ops[1].mem.index == 0: vt.add(r)
if want is None:
    for d in sorted(hist): print(f'{d:#6x} calls {hist[d]:5d} funcs {len(nf[d]):4d}')
