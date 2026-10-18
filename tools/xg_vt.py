"""xg_vt.py XG.dll [export] - vtables stored by an xg_x.dll export (default XGCreateTexture2DComputer) and by
   the functions it calls (1 level): slot, target rva, function size (.pdata), first bytes"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); IB = pe.base
name = (sys.argv[2] if len(sys.argv) > 2 else 'XGCreateTexture2DComputer').encode()
d = pe.d; e = struct.unpack_from('<I', d, 0x3c)[0]; opt = e + 24
er = struct.unpack_from('<I', d, opt + 112)[0]; x = pe.r2o(er)
nf, nn, af, an, ao = struct.unpack_from('<IIIII', d, x + 20)
exp = None
for i in range(nn):
    s = pe.r2o(struct.unpack_from('<I', d, pe.r2o(an) + 4 * i)[0])
    if d[s:d.index(b'\0', s)] == name:
        o = struct.unpack_from('<H', d, pe.r2o(ao) + 2 * i)[0]
        exp = struct.unpack_from('<I', d, pe.r2o(af) + 4 * o)[0]
print(f'# {name.decode()} = {exp:#x}')
md = Cs(CS_ARCH_X86, CS_MODE_64)
text = (pe.text_rva, pe.text_rva + pe.text_len)
def scan(f):
    calls, vts = [], []
    code = pe.read(f[0], 0x200)                     # .pdata parte as funcoes do XG em pedacos: varre 0x200 bytes
    for a, sz, m, op in md.disasm_lite(code, f[0]):
        if m in ('ret', 'int3'): break
        if m == 'call' and op.startswith('0x'): calls.append(int(op, 16))
        if m == 'lea' and '[rip' in op:
            t = a + sz + int(op.split('rip ')[1].rstrip(']').replace(' ', ''), 16)
            if pe.r2o(t) is None: continue
            p = struct.unpack('<Q', pe.read(t, 8))[0] - IB
            if text[0] <= p < text[1]: vts.append((a, t))
    return calls, vts
seen = set()
calls, vts = scan((exp, 0))
for c in calls:
    if c not in seen: seen.add(c); vts += scan((c, 0))[1]
for site, t in vts:
    print(f'## vtable {t:#x} (lea em {site:#x})')
    for s in range(40):
        p = struct.unpack('<Q', pe.read(t + 8 * s, 8))[0] - IB
        if not (text[0] <= p < text[1]): break
        g = pe.func(p)
        print(f'  {s:2d} {p:#07x} size {g and g[1] - g[0]} {pe.read(p, 12).hex()}')
