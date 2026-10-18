"""iatrefs.py EXE va [va...] - instructions with a RIP-relative operand pointing at the given VAs (IAT slots, globals)"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP
pe = PE(sys.argv[1])
targets = {int(x, 16) for x in sys.argv[2:]}
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
data = open(sys.argv[1], 'rb').read()
import struct
e = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, e + 6)[0]
opt = struct.unpack_from('<H', data, e + 20)[0]
base = struct.unpack_from('<Q', data, e + 24 + 24)[0]
for s in range(nsec):
    o = e + 24 + opt + s * 40
    name = data[o:o + 8].rstrip(b'\0')
    vsz, rva, rsz, roff, ch = struct.unpack_from('<IIII', data, o + 8) + (struct.unpack_from('<I', data, o + 36)[0],)
    if not ch & 0x20000000:
        continue
    code = data[roff:roff + rsz]
    # decode at every start before each possible disp32 position; keep RIP operands that hit a target
    for i in range(len(code) - 4):
        for start in range(max(0, i - 4), i):
            for insn in md.disasm(code[start:start + 16], base + rva + start, 1):
                if insn.address + insn.size < base + rva + i + 4:
                    continue
                for op in insn.operands:
                    if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                        va = insn.address + insn.size + op.mem.disp
                        if va in targets and start + insn.size >= i + 4 and start < i:
                            print(hex(insn.address), insn.mnemonic, insn.op_str, '->', hex(va))
