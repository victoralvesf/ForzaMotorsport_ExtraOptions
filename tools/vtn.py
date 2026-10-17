"""vtn.py EXE CLASSNAME [slot...] : vtables of an RTTI class; prints slots"""
import sys,os,struct,re
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1]); d=pe.d; IB=pe.base
def o2rva(o):
    for nm,vs,va,rs,ro in pe.secs:
        if ro<=o<ro+rs: return va+o-ro
name=sys.argv[2].encode()
slots=[int(x) for x in sys.argv[3:]]
for m in re.finditer(re.escape(name+b'\0'),d):
    td=o2rva(m.start()-16)
    for c in re.finditer(re.escape(struct.pack('<I',td)),d):
        o=c.start()-12
        if struct.unpack_from('<I',d,o)[0]!=1: continue
        col=o2rva(o)
        for v in re.finditer(re.escape(struct.pack('<Q',IB+col)),d):
            vt=o2rva(v.start())+8; n=0; out=[]
            while True:
                q=struct.unpack_from('<Q',d,pe.r2o(vt+8*n))[0]-IB
                if not(pe.text_rva<=q<pe.text_rva+pe.text_len): break
                out.append(q); n+=1
            print(sys.argv[2],'vtable',hex(vt),'%d slots'%n, ' '.join('#%d=%s'%(s,hex(out[s]) if s<n else '-') for s in slots))
