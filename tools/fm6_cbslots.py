"""fm6_cbslots.py EXE [slot] - chamadas do FM6 aos XXSetConstantBuffers da tabela embutida do contexto
([ctx+0x3d0+8*s]: VS 0x09, HS 0x3f, DS 0x43, GS 0x18, PS 0x12, CS 0x48) com o StartSlot (edx) e NumBuffers (r8d)
vistos no mesmo bloco antes do call (disassembly linear desde o inicio da funcao do .pdata). Com slot, so as que
cobrem esse slot (ou de valor desconhecido)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_REG

STAGES = {0x418: 'VS', 0x5c8: 'HS', 0x5e8: 'DS', 0x490: 'GS', 0x460: 'PS', 0x610: 'CS'}
pe = PE(sys.argv[1])
want = int(sys.argv[2]) if len(sys.argv) > 2 else None
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
REGS = {'edx': 'edx', 'rdx': 'edx', 'dl': 'edx', 'r8d': 'r8d', 'r8': 'r8d', 'r8b': 'r8d'}
for disp, st in STAGES.items():
    pat = struct.pack('<I', disp)
    i = pe.text.find(pat)
    while i >= 0:
        rva = pe.text_rva + i
        f = pe.func(rva)
        if f and pe.text[i - 1] in (0x90, 0x91, 0x92, 0x93, 0x95, 0x96, 0x97) and pe.text[i - 2] == 0xff:
            call = rva - 2 - (1 if pe.text[i - 3] in (0x41, 0x49) else 0)
            val = {}
            for ins in md.disasm(pe.read(f[0], call - f[0] + 8), f[0]):
                if ins.address >= call: break
                if ins.mnemonic.startswith('j') or ins.mnemonic in ('call', 'ret'):
                    if ins.mnemonic == 'call': val.pop('edx', None); val.pop('r8d', None)
                    continue
                ops = ins.operands
                if ops and ops[0].type == X86_OP_REG:
                    r = REGS.get(ins.reg_name(ops[0].reg))
                    if not r: continue
                    if ins.mnemonic == 'mov' and len(ops) == 2 and ops[1].type == X86_OP_IMM: val[r] = ops[1].imm
                    elif ins.mnemonic == 'xor' and len(ops) == 2 and ops[1].type == X86_OP_REG and ops[1].reg == ops[0].reg:
                        val[r] = 0
                    elif ins.mnemonic == 'lea' and len(ops) == 2: val[r] = ins.op_str.split(',', 1)[1].strip()
                    else: val[r] = '?' + ins.mnemonic
            s, n = val.get('edx', '?'), val.get('r8d', '?')
            hit = want is None or not isinstance(s, int) or not isinstance(n, int) or s <= want < s + n
            if hit: print(f'{st} call game+{call:#x} (funcao {f[0]:#x}) start={s} n={n}')
        i = pe.text.find(pat, i + 1)
