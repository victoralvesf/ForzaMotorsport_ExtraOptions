"""fm6_present_mode_callers.py EXE : call [reg+0x80] com 0x1e/0x3c nas 20 instrucoes antes (pedido de modo de present)"""
import sys,os,re
sys.path.insert(0,os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=PE(sys.argv[1]); d=pe.d
t0=pe.r2o(pe.text_rva); t=d[t0:t0+pe.text_len]
md=Cs(CS_ARCH_X86,CS_MODE_64)
for m in re.finditer(rb'\xff[\x90-\x97]\x80\x00\x00\x00|\x41\xff[\x90-\x97]\x80\x00\x00\x00',t):
    end=pe.text_rva+m.start()
    ins=list(md.disasm(pe.read(end-120,140),end-120))
    win=[i for i in ins if i.address<=end][-20:]
    txt=' ; '.join('%s %s'%(i.mnemonic,i.op_str) for i in win)
    if re.search(r'\b0x1e\b|\b0x3c\b',txt) and win and win[-1].address==end:
        print(hex(end)); print('   ',txt[-600:])
