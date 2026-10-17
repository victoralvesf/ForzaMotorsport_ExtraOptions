"""dispscan.py EXE disp... [--in f1,f2]  - instructions with [reg+disp] (reg != rsp/rbp/rip), grouped by function"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM
from capstone.x86 import X86_REG_RSP, X86_REG_RBP, X86_REG_RIP, X86_REG_INVALID
pe = PE(sys.argv[1]); args = sys.argv[2:]; only = None
if '--in' in args:
    i = args.index('--in'); only = {int(x, 16) for x in args[i+1].split(',')}; args = args[:i] + args[i+2:]
ds = {int(x, 16) for x in args}
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
hits = collections.defaultdict(list)
for b, e in pe.funcs:
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    if only and b not in only: continue
    for ins in md.disasm(pe.read(b, e - b), b):
        for op in ins.operands:
            if op.type == CS_OP_MEM and op.mem.disp in ds and op.mem.base not in (X86_REG_RSP, X86_REG_RBP, X86_REG_RIP, X86_REG_INVALID):
                hits[b].append(f'{ins.address:#x} {ins.mnemonic} {ins.op_str}')
for f, l in sorted(hits.items()):
    print(f'func {f:#x}: {len(l)}'); [print('   ', x) for x in l[:12]]
