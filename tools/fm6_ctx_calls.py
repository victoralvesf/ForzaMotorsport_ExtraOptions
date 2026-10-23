"""fm6_ctx_calls.py EXE OFF [OFF...] : call/jmp [reg+OFF] (ex.: 0x538 viewport, 0x540 scissor da tabela do contexto
do FM6 em +0x3d8) com a funcao (.pdata) de cada site."""
import sys,os,re,collections
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=PE(sys.argv[1]);md=Cs(CS_ARCH_X86,CS_MODE_64)
for a in sys.argv[2:]:
    off=int(a,16); d=off.to_bytes(4,'little'); out=collections.defaultdict(list)
    for m in re.finditer(re.escape(d),pe.text):
        j=m.start()
        for k in (2,3):
            s=j-k
            for i in md.disasm(pe.text[s:s+k+4],pe.text_rva+s):
                if i.size==k+4 and i.mnemonic in('call','jmp') and ('+ %s]'%a.lower()) in i.op_str:
                    f=pe.func(i.address); out[f and f[0]].append((hex(i.address),i.mnemonic+' '+i.op_str))
                break
    print('==',a,sum(len(v) for v in out.values()),'sites')
    for f,v in sorted(out.items(),key=lambda x:x[0] or 0): print('  func',f and hex(f),v)
