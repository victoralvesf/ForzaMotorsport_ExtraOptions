"""fm6_const_refs.py EXE : constantes de passo (1/59.94, 1/60, 60, 59.94 em float/double) e instrucoes rip que as leem"""
import sys,os,re,struct
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=PE(sys.argv[1]); d=pe.d
def o2rva(o):
    for nm,vs,va,rs,ro in pe.secs:
        if ro<=o<ro+rs: return va+o-ro
t0=pe.r2o(pe.text_rva); t=d[t0:t0+pe.text_len]
md=Cs(CS_ARCH_X86,CS_MODE_64)
consts={}
for v in (1/59.94,1/60.0,0.016683350,1/30.0,1/29.97):
    consts[struct.pack('<f',v)]='f%.7f'%v; consts[struct.pack('<d',v)]='d%.9f'%v
targets={}
for b,n in consts.items():
    for m in re.finditer(re.escape(b),d):
        r=o2rva(m.start())
        if r is not None and not (pe.text_rva<=r<pe.text_rva+pe.text_len): targets[r]=n
# rip-relative disp32 scan: any position j where j+k+4+disp == target, k in instruction tail (imm8 or none)
hits=[]
for j in range(len(t)-4):
    disp=struct.unpack_from('<i',t,j)[0]
    for tail in (0,1):
        x=pe.text_rva+j+4+tail+disp
        if x in targets:
            hits.append((pe.text_rva+j,x,tail))
for j,x,tail in hits:
    # try to decode an instruction ending at j+4+tail
    for back in range(2,8):
        s=j-back
        ins=list(md.disasm(pe.read(s,16),s))
        if ins and ins[0].address+ins[0].size==j+4+tail and 'rip' in ins[0].op_str:
            print(hex(ins[0].address),targets[x],ins[0].mnemonic,ins[0].op_str); break
