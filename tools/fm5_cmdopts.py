"""fm5_cmdopts.py EXE [min_off max_off] - lista as opcoes de linha de comando do FM5 registradas no parser
0x5d24d0 (CForzaCommandLineParameters): 'lea r8,[rsi+off]' + 'lea rdx,[rip+nome]' + call leitor.
Mostra offset no objeto de opcoes (estatico 0x1425d04b0), nome e o leitor chamado (0x681450 = bool)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1])
lo, hi = (int(sys.argv[2], 16), int(sys.argv[3], 16)) if len(sys.argv) > 3 else (0, 1 << 30)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = False
f0, f1 = 0x5d24d0, 0x5d4242
code = pe.d[pe.r2o(f0):pe.r2o(f1)]
def cstr(rva):
    o = pe.r2o(rva); e = pe.d.index(b'\0', o); return pe.d[o:e].decode('latin1')
off = name = None
for ins in md.disasm(code, f0):
    s = ins.op_str
    if ins.mnemonic == 'lea' and s.startswith('r8, [rsi + '):
        off = int(s.split('+ ')[1].rstrip(']'), 16)
    elif ins.mnemonic == 'lea' and s.startswith('rdx, [rip'):
        d = int(s.split('+ ')[1].rstrip(']'), 16) if '+' in s else -int(s.split('- ')[1].rstrip(']'), 16)
        try: name = cstr(ins.address + ins.size + d)
        except Exception: name = None
    elif ins.mnemonic == 'call':
        if off is not None and name and lo <= off <= hi:
            print(f'{ins.address:#x} +{off:#06x} {name:40s} -> {s}')
        off = name = None
