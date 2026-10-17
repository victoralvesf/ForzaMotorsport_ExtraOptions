"""immscan.py EXE imm... - add/sub/cmp/mov with immediate operand in the set, grouped by function"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM
pe = PE(sys.argv[1]); ims = {int(x, 16) for x in sys.argv[2:]}
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
for b, e in pe.funcs:
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    for ins in md.disasm(pe.read(b, e - b), b):
        if ins.mnemonic in ('add', 'sub', 'mov', 'cmp', 'imul') and any(op.type == CS_OP_IMM and op.imm in ims for op in ins.operands):
            print(f'{b:#x} {ins.address:#x} {ins.mnemonic} {ins.op_str}')
