"""calls.py EXE IATPKL FUNC_RVA... : list calls (import names / direct targets / vcalls) and string refs"""
import sys,os,pickle,struct
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
md=Cs(CS_ARCH_X86,CS_MODE_64)
pe=PE(sys.argv[1]); iat=pickle.load(open(sys.argv[2],'rb'))
def s_at(rva):
    o=pe.r2o(rva)
    if o is None: return None
    b=pe.d[o:o+80]
    if b[1:2]==b'\0' and 32<=b[0]<127:
        w=b.decode('utf-16le','ignore').split('\0')[0]
        return 'L"%s"'%w if len(w)>=4 else None
    s=b.split(b'\0')[0]
    return '"%s"'%s.decode('latin1') if len(s)>=4 and all(32<=c<127 for c in s) else None
for a in sys.argv[3:]:
    f=pe.func(int(a,16)); print('==',hex(f[0]),hex(f[1]))
    for ad,sz,m,op in md.disasm_lite(pe.read(f[0],f[1]-f[0]),f[0]):
        b=pe.read(ad,sz); note=None
        if 'rip' in op and sz>=6:
            for k in range(sz-3):
                pass
            disp=struct.unpack('<i',b[-4:])[0] if not op.endswith(('0x1','0x0')) else None
            # find disp: try each offset
            for off in range(2,sz-3):
                t=ad+sz+struct.unpack('<i',b[off:off+4])[0]
                if t in iat: note=iat[t]; break
                st=s_at(t)
                if st and m=='lea': note=st; break
        if m=='call' and not note and op.startswith('0x'): note='call '+op
        if m=='call' and not note: note='call '+op
        if note: print('  %#x %s'%(ad,note))
