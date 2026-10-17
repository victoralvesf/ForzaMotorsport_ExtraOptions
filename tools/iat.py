import sys,os,struct
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
pe=PE(sys.argv[1]); d=pe.d
e=struct.unpack_from('<I',d,0x3c)[0]; opt=e+24
ir,isz=struct.unpack_from('<II',d,opt+112+8)
x=pe.r2o(ir); m={}
while True:
    oft,ts,fc,nm,ft=struct.unpack_from('<IIIII',d,x); x+=20
    if not nm: break
    dll=d[pe.r2o(nm):].split(b'\0')[0].decode()
    t=pe.r2o(oft or ft); k=0
    while True:
        v=struct.unpack_from('<Q',d,t+8*k)[0]
        if not v: break
        name='#%d'%(v&0xffff) if v>>63 else d[pe.r2o(v&0x7fffffff)+2:].split(b'\0')[0].decode()
        m[ft+8*k]=dll+'!'+name; k+=1
import pickle; pickle.dump(m,open(sys.argv[2],'wb'))
for a in sys.argv[3:]:
    print(a, m.get(int(a,16)))
