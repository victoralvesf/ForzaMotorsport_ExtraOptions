"""vcalls.py EXE [min_disp] [--sites disp] - histograma de 'call [vtbl+disp]' (vtbl = mov r,[r2] antes)
   --sites disp: lista os sites dessa disp"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_REG
from capstone.x86 import X86_REG_RIP, X86_REG_RSP
pe = PE(sys.argv[1]); args = sys.argv[2:]
want = None
if '--sites' in args:
    i = args.index('--sites'); want = int(args[i+1], 16); args = args[:i] + args[i+2:]
mind = int(args[0], 16) if args else 0
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
hist = collections.Counter(); nf = collections.defaultdict(set)
for b, e in pe.funcs:
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    vt = {}   # reg -> (base reg, disp) of the load
    for ins in md.disasm(pe.read(b, e - b), b):
        ops = ins.operands
        if ins.mnemonic == 'call' and len(ops) == 1 and ops[0].type == CS_OP_MEM:
            m = ops[0].mem
            if m.base in vt and m.index == 0 and m.disp >= mind:
                hist[m.disp] += 1; nf[m.disp].add(b)
                if want == m.disp: print(f'{b:#x} {ins.address:#x} obj=[{ins.reg_name(vt[m.base][0])}+{vt[m.base][1]:#x}]')
            vt.clear(); continue
        if ins.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == CS_OP_REG:
            r = ops[0].reg
            for k in [k for k, v in vt.items() if v[0] == r or k == r]: vt.pop(k, None)
            if ops[1].type == CS_OP_MEM and ops[1].mem.disp == 0 and ops[1].mem.index == 0 and ops[1].mem.base not in (X86_REG_RIP, X86_REG_RSP, 0) and ops[0].size == 8:
                vt[r] = (ops[1].mem.base, 0)
            continue
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'ret':
            pass
if want is None:
    for d in sorted(hist): print(f'{d:#6x} slot {d//8:#5x}  calls {hist[d]:5d}  funcs {len(nf[d]):4d}')
