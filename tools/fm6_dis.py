"""fm6_dis.py EXE RVA [N] - disassembla N instrucoes (padrao 60) a partir de RVA (capstone)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); r = int(sys.argv[2], 0); n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 60
for i in list(Cs(CS_ARCH_X86, CS_MODE_64).disasm(pe.read(r, n * 15), r))[:n]:
    print(f'{i.address:#x} {i.bytes.hex():24s} {i.mnemonic} {i.op_str}')
