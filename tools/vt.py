"""vt.py EXE FUNC_RVA : find vtables containing FUNC (as VA), print slot and RTTI class name"""
import sys,os,struct,re
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1]); d=pe.d; IB=pe.base
def rva2o(r): return pe.r2o(r)
def o2rva(o):
    for nm,vs,va,rs,ro in pe.secs:
        if ro<=o<ro+rs: return va+o-ro
def rtti(vt_rva):
    col=struct.unpack_from('<Q',d,rva2o(vt_rva-8))[0]-IB
    o=rva2o(col)
    if o is None: return None
    sig,off,cdo,td,chd=struct.unpack_from('<IIIII',d,o)
    if sig!=1: return None
    to=rva2o(td)
    return d[to+16:to+200].split(b'\0')[0].decode('latin1')
for a in sys.argv[2:]:
    va=struct.pack('<Q',IB+int(a,16))
    for m in re.finditer(re.escape(va),d):
        r=o2rva(m.start())
        # walk back to vtable start: previous qword is COL pointer when the qword before slot0 isn't code
        s=r
        while True:
            q=struct.unpack_from('<Q',d,rva2o(s-8))[0]
            t=q-IB
            if pe.text_rva<=t<pe.text_rva+pe.text_len: s-=8
            else: break
        print(a,'em vtable',hex(s),'slot',hex(r-s),'(#%d)'%((r-s)//8),'classe',rtti(s))
