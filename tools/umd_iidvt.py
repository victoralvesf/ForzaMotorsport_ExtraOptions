"""umd_iidvt.py UMD.dll iid_rva... - pointer tables in .rdata/.data whose slot 0 (QueryInterface) references
   one of the IIDs (rip-relative lea/mov or cmp against [rip+x]); prints table, slot count and the IIDs hit"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d; IB = pe.base
want = {int(x, 16) for x in sys.argv[2:]}
text = (pe.text_rva, pe.text_rva + pe.text_len)
def refs(q):
    f = pe.func(q)
    if not f: return set()
    code = pe.read(f[0], f[1] - f[0]); out = set()
    for i in range(len(code) - 7):
        if (code[i + 2] & 0xc7) == 5 and code[i + 1] in (0x8d, 0x8b, 0x3b, 0x2b) and code[i] in (0x48, 0x4c, 0x49):
            out.add(f[0] + i + 7 + struct.unpack_from('<i', code, i + 3)[0])
    return out
for nm, vs, va, rs, ro in pe.secs:
    if nm.rstrip(b'\0') not in (b'.rdata', b'.data'): continue
    o = ro
    while o < ro + rs - 8:
        q = struct.unpack_from('<Q', d, o)[0] - IB
        if text[0] <= q < text[1]:
            hit = refs(q) & want
            if hit:
                n = 0
                while o + 8 * n + 8 <= ro + rs and text[0] <= struct.unpack_from('<Q', d, o + 8 * n)[0] - IB < text[1]: n += 1
                print(f'table {va + o - ro:#x} slots {n:#x} QI {q:#x} iids {sorted(hex(h) for h in hit)}')
        o += 8
