"""fm6_vt_of.py EXE RTTI_NAME... : vtables da classe (pelo COL) e instrucoes lea/mov rip que as referenciam"""
import sys,os,re,struct
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1]); d=pe.d; IB=pe.base
def o2rva(o):
    for nm,vs,va,rs,ro in pe.secs:
        if ro<=o<ro+rs: return va+o-ro
t0=pe.r2o(pe.text_rva); t=d[t0:t0+pe.text_len]
for name in sys.argv[2:]:
    for m in re.finditer(re.escape(name.encode()+b'\0'),d):
        td=o2rva(m.start())-0x10
        for c in re.finditer(re.escape(struct.pack('<I',td)),d):
            o=c.start()-12
            if struct.unpack_from('<I',d,o)[0]!=1: continue
            col=IB+o2rva(o)
            for k in re.finditer(re.escape(struct.pack('<Q',col)),d):
                vt=o2rva(k.start())+8
                slots=[struct.unpack_from('<Q',d,pe.r2o(vt+8*i))[0]-IB for i in range(4)]
                print(name,'vtable',hex(vt),'slots',[hex(s) for s in slots])
                for j in range(len(t)-7):
                    if t[j] in (0x48,0x4c) and t[j+1] in (0x8d,0x8b) and (t[j+2]&0xc7)==5:
                        r=pe.text_rva+j
                        if r+7+struct.unpack_from('<i',t,j+3)[0]==vt: print('   ref',hex(r))
