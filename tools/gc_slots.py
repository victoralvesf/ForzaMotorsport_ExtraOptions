"""gc_slots.py DLL VTRVA N - per slot of a vtable: thunk rva, thunk size, callees (rva:size) for diffing interface layouts"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); vt = int(sys.argv[2], 16); n = int(sys.argv[3])
md = Cs(CS_ARCH_X86, CS_MODE_64)
def fsize(r):
    f = pe.func(r); return (f[1] - f[0]) if f else -1
for i in range(n):
    t = struct.unpack('<Q', pe.read(vt + 8 * i, 8))[0] - pe.base
    f = pe.func(t); end = f[1] if f else t + 0x80
    calls = []; vcalls = []
    for a, s, m, op in md.disasm_lite(pe.read(t, end - t), t):
        if m in ('call', 'jmp') and op.startswith('0x'):
            c = int(op, 16)
            if not (t <= c < end): calls.append(f'{c:#x}:{fsize(c):#x}')
        elif m == 'call' and 'ptr [r' in op: vcalls.append(op.split('[')[1].rstrip(']'))
    print(f'#{i:2d} {t:#x} sz={end - t:#x} calls={" ".join(calls)} v={" ".join(vcalls)}')
