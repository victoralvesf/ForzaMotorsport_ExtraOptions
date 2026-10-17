"""fm5_dynrender.py EXE [filtro] - lista os controles CDynamicRender*Control do FM5 (RTTI):
vtable, id (slot 0), categoria (slot 1), nome (slot 2), default (slot 4: bytes copiados) e campos (slot 6:
nomes passados ao leitor [vtable+0x138] etc.). Usado p/ achar CarLODThresholds/CarFocusLODMinMax (car_detail)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); d = pe.d; IB = pe.base
flt = sys.argv[2].lower() if len(sys.argv) > 2 else ''
md = Cs(CS_ARCH_X86, CS_MODE_64)
def o2rva(o):
    for nm, vs, va, rs, ro in pe.secs:
        if ro <= o < ro + rs: return va + o - ro
def cstr(r):
    try:
        o = pe.r2o(r); s = d[o:d.index(b'\0', o)]
        return s.decode('latin1') if 0 < len(s) < 80 and all(32 <= c < 127 for c in s) else None
    except Exception: return None
def dis(r, n=60):
    o = pe.r2o(r); return list(md.disasm(d[o:o + n * 8], r))[:n]
def ripref(ins):
    m = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', ins.op_str)
    if m: return ins.address + ins.size + (int(m.group(2), 16) * (1 if m.group(1) == '+' else -1))
def vtables(name):
    out = []
    for m in re.finditer(re.escape(name + b'\0'), d):
        td = o2rva(m.start() - 16)
        for c in re.finditer(re.escape(struct.pack('<I', td)), d):
            o = c.start() - 12
            if struct.unpack_from('<I', d, o)[0] != 1: continue
            col = o2rva(o)
            for v in re.finditer(re.escape(struct.pack('<Q', IB + col)), d):
                vt = o2rva(v.start()) + 8; sl = []
                while True:
                    q = struct.unpack_from('<Q', d, pe.r2o(vt + 8 * len(sl)))[0] - IB
                    if not (pe.text_rva <= q < pe.text_rva + pe.text_len): break
                    sl.append(q)
                out.append((vt, sl))
    return out
for m in re.finditer(rb'\.\?AVCDynamicRender(\w+)Control@@\0', d):
    cls = m.group(0)[:-1]
    if flt not in cls.decode().lower(): continue
    for vt, sl in vtables(cls):
        if len(sl) < 7: continue
        i0 = dis(sl[0], 1)[0]; ident = i0.op_str if i0.mnemonic == 'mov' else '?'
        cat = cstr(ripref(dis(sl[1], 1)[0]) or 0); nm = cstr(ripref(dis(sl[2], 1)[0]) or 0)
        dflt = ''
        for ins in dis(sl[4], 12):
            r = ripref(ins)
            if r and ('xmm' in ins.op_str or 'ymm' in ins.op_str):
                raw = d[pe.r2o(r):pe.r2o(r) + (32 if 'ymm' in ins.op_str else 16)]
                dflt += ' raw ' + raw.hex()
            elif ins.mnemonic == 'mov' and 'ptr [rcx' in ins.op_str and ', 0x' in ins.op_str:
                dflt += ' ' + ins.op_str
            if ins.mnemonic in ('ret', 'jmp'): break
        flds = [cstr(ripref(ins)) for ins in dis(sl[6], 120) if ins.mnemonic == 'lea' and ripref(ins)]
        print(f'{cls.decode()[16:-2]:45s} vt {vt:#x} id {ident} {cat}/{nm} slots {" ".join(hex(x) for x in sl)}\n'
              f'    default:{dflt}\n    campos: {[f for f in flds if f]}')
