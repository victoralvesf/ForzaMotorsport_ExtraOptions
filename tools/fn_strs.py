"""fn_strs.py EXE 0xRVA [...] : strings (ASCII/UTF-16) referenciadas por lea/mov [rip+X] em cada funcao (.pdata) que
contem o RVA; serve para achar a funcao equivalente em outro exe pelas mesmas strings."""
import sys,os,re
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=PE(sys.argv[1]);md=Cs(CS_ARCH_X86,CS_MODE_64)
def s_at(rva):
    if pe.r2o(rva) is None: return None
    b=pe.read(rva,200)
    if not b: return None
    m=re.match(rb'[\x20-\x7e]{4,}\x00',b)
    if m: return m.group()[:-1].decode()
    m=re.match(rb'(?:[\x20-\x7e]\x00){4,}\x00\x00',b)
    if m: return 'L"'+m.group()[:-2].decode('utf-16le')+'"'
for a in sys.argv[2:]:
    f=pe.func(int(a,16)); print('== func',hex(f[0]),hex(f[1]))
    for i in md.disasm(pe.read(f[0],f[1]-f[0]),f[0]):
        if 'rip +' in i.op_str or 'rip -' in i.op_str:
            m=re.search(r'rip ([+-]) (0x[0-9a-f]+)',i.op_str); x=i.address+i.size+(int(m.group(2),16)*(1 if m.group(1)=='+' else -1))
            s=s_at(x)
            if s: print('  %#x %#x %r'%(i.address,x,s))
