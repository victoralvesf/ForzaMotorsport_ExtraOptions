"""xdis_ext.py DLL 0xRVA [len] - disassembla a partir do RVA e anota strings ASCII/UTF-16 dos operandos RIP-relativos."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM
from capstone.x86 import X86_REG_RIP
pe = PE(sys.argv[1]); s = int(sys.argv[2], 16); n = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x200
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
def sat(rva):
    if pe.r2o(rva) is None: return None
    b = pe.read(rva, 120)
    a = b.split(b'\0')[0]
    if len(a) >= 4 and all(32 <= c < 127 for c in a): return a.decode()
    w = b.split(b'\0\0')[0]
    if len(w) >= 8 and all(32 <= c < 127 for c in w[::2]) and not any(w[1::2]): return 'L"' + w[::2].decode() + '"'
for ins in md.disasm(pe.read(s, n), s):
    x = ''
    for op in ins.operands:
        if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP:
            t = ins.address + ins.size + op.mem.disp; st = sat(t); x = f'  ; {t:#x}' + (f' "{st}"' if st else '')
    print(f'  {ins.address:#x} {ins.mnemonic} {ins.op_str}{x}')
