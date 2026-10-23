"""fm6_res_sites.py EXE : procura no exe (FM6) os sites de resolucao portados do FM5 (16_opcoes_graficas.txt,
PASSO 9 cont. 1-3) por regex de bytes e desmonta cada ocorrencia."""
import sys,os,re
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=PE(sys.argv[1]); md=Cs(CS_ARCH_X86,CS_MODE_64)
pats={
 'altura (mov dword [rbx+X],1080)':rb'\xc7\x83..\x00\x00\x38\x04\x00\x00',
 'largura (vmulss;vmulss;vcvttss2si rbp,xmm2)':rb'\xc5\xfa\x59\x0d....\xc5\xf2\x59\x15....\xc4\xe1\xfa\x2c\xea',
 'tamanho da UI ([r15+8]/[r15+0xc] -> floats)':rb'\x41\x8b\x47\x08\xc5\xf8\x57\xc0\xc4\xe1\xfa\x2a\xc0\xc5\xfa\x11\x45.\x41\x8b\x47\x0c',
 'viewport da UI (call [rax+0x188])':rb'\xff\x90\x88\x01\x00\x00',
}
sel=sys.argv[2:] or list(pats)
for nm,p in pats.items():
    if not any(s in nm for s in sel): continue
    hits=[m.start() for m in re.finditer(p,pe.text,re.S)]
    print('==',nm,len(hits))
    for j in hits[:12]:
        r=pe.text_rva+j; f=pe.func(r)
        print('  site %#x func %s'%(r,hex(f[0]) if f else '?'))
        for i in md.disasm(pe.read(r-0x10,0x40),r-0x10):
            print('    %#x %-24s %s %s'%(i.address,i.bytes.hex(),i.mnemonic,i.op_str))
