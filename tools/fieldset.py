"""fieldset.py EXE DISP[,DISP...] [--write DISP] : funcoes (.pdata) cujas instrucoes usam [reg+DISP] para TODOS os DISP
dados; com --write, lista so as que escrevem nesse DISP (mov/add/sub/inc/dec/xadd/cmpxchg com destino na memoria)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM

pe = PE(sys.argv[1])
want = [int(x, 16) for x in sys.argv[2].split(",")]
wr = int(sys.argv[sys.argv.index("--write") + 1], 16) if "--write" in sys.argv else None
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
WOPS = {"mov", "add", "sub", "inc", "dec", "xadd", "cmpxchg", "and", "or", "xchg"}
for b, e in pe.funcs:
    if e - b > 0x4000: continue
    seen, writes = set(), []
    for ins in md.disasm(pe.read(b, e - b), b):
        for k, op in enumerate(ins.operands):
            if op.type == CS_OP_MEM and op.mem.base and op.mem.disp in want:
                seen.add(op.mem.disp)
                if wr is not None and op.mem.disp == wr and k == 0 and ins.mnemonic in WOPS:
                    writes.append(f"{ins.address:#x} {ins.mnemonic} {ins.op_str}")
    if len(seen) == len(want) and (wr is None or writes):
        print(f"func {b:#x}-{e:#x}: " + " | ".join(writes))
