"""fm6_frame_vt.py: vtables .?AVFrame@@ / .?AVFrame11@@ do FM6 e bytes dos slots do present/limitador"""
import sys,os,re,struct
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1] if len(sys.argv)>1 else 'game_binaries/FM6/forza_durango_release_final.exe'); d=pe.d; IB=pe.base
def o2rva(o):
    for nm,vs,va,rs,ro in pe.secs:
        if ro<=o<ro+rs: return va+o-ro
for m in re.finditer(rb'\.\?AVFrame(11)?@@\0',d):
    td=o2rva(m.start())-0x10
    for i in range(0,len(d)-20,4):
        if struct.unpack_from('<I',d,i)[0]==1 and struct.unpack_from('<I',d,i+12)[0]==td:
            col=IB+o2rva(i)
            for k in re.finditer(re.escape(struct.pack('<Q',col)),d):
                vt=o2rva(k.start())+8
                print(m.group(0).decode(),'vtable',hex(vt))
                for s in (0xe8,0x138,0x180,0x188):
                    f=struct.unpack_from('<Q',d,pe.r2o(vt+s))[0]-IB
                    print('   slot %#x -> %#x  %s'%(s,f,d[pe.r2o(f):pe.r2o(f)+32].hex()))
# todos os slots curtos (setter/getter) do Frame11
if '--all' in sys.argv:
    vt=0x19f38e8
    for s in range(0,0x400,8):
        f=struct.unpack_from('<Q',d,pe.r2o(vt+s))[0]-IB
        if not (pe.text_rva<=f<pe.text_rva+pe.text_len): break
        b=d[pe.r2o(f):pe.r2o(f)+16]
        print('   slot %#x -> %#x  %s'%(s,f,b.hex()))
