"""umd_vt.py UMD.dll [min_len] - vtables (runs of pointers into .text) in the OS driver umd.dll;
   marks the ones whose slot-0 QueryInterface references IID_ID3D11DeviceX / IID_ID3D11DeviceContextX"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d; IB = pe.base
mn = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x30
starts = set(pe.fstarts)
text = (pe.text_rva, pe.text_rva + pe.text_len)
IIDS = {'DeviceX': bytes.fromhex('101667882e711e4f84ab01b5948bd373')}
iid_rva = {}
for k, v in IIDS.items():
    for m in re.finditer(re.escape(v), d):
        for nm, vs, va, rs, ro in pe.secs:
            if ro <= m.start() < ro + rs: iid_rva.setdefault(k, []).append(va + m.start() - ro)
def refs(f):  # rip-relative targets inside a function (lea/mov ... [rip+x])
    out = set(); code = pe.read(f[0], f[1] - f[0])
    for i in range(len(code) - 7):
        if code[i] in (0x48, 0x4c) and code[i + 1] in (0x8d, 0x8b) and (code[i + 2] & 0xc7) == 5:
            out.add(f[0] + i + 7 + struct.unpack_from('<i', code, i + 3)[0])
    return out
for nm, vs, va, rs, ro in pe.secs:
    if nm.rstrip(b'\0') not in (b'.rdata', b'.data'): continue
    o = ro; end = ro + rs
    while o < end:
        n = 0
        while o + 8 * n + 8 <= end:
            q = struct.unpack_from('<Q', d, o + 8 * n)[0] - IB
            if not (text[0] <= q < text[1]): break
            n += 1
        if n >= mn:
            vt = va + o - ro; s0 = struct.unpack_from('<Q', d, o)[0] - IB
            f = pe.func(s0); tag = [k for k, r in iid_rva.items() if f and any(x in refs(f) for x in r)]
            print(f'vtable {vt:#x} slots {n:#x} slot0 {s0:#x} {tag}')
            o += 8 * n
        else: o += 8
