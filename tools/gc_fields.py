"""gc_fields.py DLL VTRVA FIRST LAST - for C++/CX property impls in a class vtable: first [rcx+disp] / [reg+disp] operands (field offsets) per slot"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); vt = int(sys.argv[2], 16); a, b = int(sys.argv[3]), int(sys.argv[4])
md = Cs(CS_ARCH_X86, CS_MODE_64)
for i in range(a, b + 1):
    t = struct.unpack('<Q', pe.read(vt + 8 * i, 8))[0] - pe.base
    f = pe.func(t); end = f[1] if f else t + 0x60
    ops = []
    for ad, s, m, op in md.disasm_lite(pe.read(t, end - t), t):
        for d in re.findall(r'\[r(?:cx|dx|bx|di|si|8|9|ax) \+ (0x[0-9a-f]+)\]', op):
            if not op.startswith('qword ptr [rsp'): ops.append(f'{m}:{d}')
        if m == 'ret': break
    print(f'#{i:2d} {t:#x} {" ".join(ops[:6])}')
