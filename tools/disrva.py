"""disrva.py EXE 0xRVA [count] - disassemble count instructions from an RVA (capstone)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); r = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
for k, ins in enumerate(Cs(CS_ARCH_X86, CS_MODE_64).disasm(pe.read(r, n * 15), r)):
    if k >= n: break
    print(f'{ins.address:#x}: {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}')
