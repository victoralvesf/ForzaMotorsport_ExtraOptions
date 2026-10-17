"""fcalls.py EXE fieldoffs(comma) [min_disp] [--sites] - chamadas virtuais em objetos carregados de [reg+field]
   (mov rA,[rB+field] ; mov rC,[rA] ; call [rC+disp]) -> histograma de disp"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_REG
from capstone.x86 import X86_REG_RIP, X86_REG_RSP
pe = PE(sys.argv[1]); fields = {int(x, 16) for x in sys.argv[2].split(',')}
mind = int(sys.argv[3], 16) if len(sys.argv) > 3 and not sys.argv[3].startswith('-') else 0
sites = '--sites' in sys.argv; direct = '--direct' in sys.argv
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
from capstone import x86 as X
KEEP = {X.X86_REG_RBX, X.X86_REG_RBP, X.X86_REG_RSI, X.X86_REG_RDI, X.X86_REG_R12, X.X86_REG_R13, X.X86_REG_R14, X.X86_REG_R15}
hist = collections.Counter(); ex = collections.defaultdict(list)
def kill(st, r):
    st.pop(r, None)
for b, e in pe.funcs:
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    obj = {}; vt = {}
    for ins in md.disasm(pe.read(b, e - b), b):
        ops = ins.operands
        if ins.mnemonic == 'call':
            if len(ops) == 1 and ops[0].type == CS_OP_MEM:
                m = ops[0].mem
                if (m.base in (obj if direct else vt)) and m.index == 0 and m.disp >= mind:
                    hist[m.disp] += 1; ex[m.disp].append((b, ins.address))
            for st in (obj, vt):
                for k in [k for k in st if k not in KEEP]: st.pop(k)
            continue
        if ins.mnemonic in ('mov', 'lea') and len(ops) == 2 and ops[0].type == CS_OP_REG:
            r = ops[0].reg
            # aliases of the same 64-bit reg are ignored (rough)
            src = ops[1]
            o = obj.get(src.reg) if src.type == CS_OP_REG else None
            obj.pop(r, None); vt.pop(r, None)
            if ins.mnemonic == 'mov' and src.type == CS_OP_MEM and src.mem.index == 0:
                if src.mem.disp in fields and src.mem.base not in (X86_REG_RIP, X86_REG_RSP): obj[r] = 1
                elif src.mem.disp == 0 and src.mem.base in obj: vt[r] = 1
            elif ins.mnemonic == 'mov' and o: obj[r] = 1
            continue
        if ops and ops[0].type == CS_OP_REG and ins.mnemonic not in ('cmp', 'test', 'push'):
            obj.pop(ops[0].reg, None); vt.pop(ops[0].reg, None)
for d in sorted(hist):
    print(f'{d:#6x} slot {d//8:#5x} calls {hist[d]:4d} ' + (' '.join(f'{a:#x}' for _, a in ex[d][:6]) if sites else ''))
