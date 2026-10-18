"""pattern_refs.py DLL hexdword [min_repeat] - ocorrencias do dword repetido (ex.: deadbeef x2) em TODAS as secoes
e, para as que estao fora do .text, as instrucoes com operando RIP-relativo que apontam para elas (ex.: constante
SSE de preenchimento em .rdata). Complementa imm_sites.py (que so olha imediatos no .text)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM
from capstone.x86 import X86_REG_RIP

pe = PE(sys.argv[1]); dw = struct.pack('<I', int(sys.argv[2], 16))
rep = int(sys.argv[3]) if len(sys.argv) > 3 else 2
pat = dw * rep
hits = []
for nm, vs, va, rs, ro in pe.secs:
    blob = pe.d[ro:ro + rs]; i = blob.find(pat)
    while i >= 0:
        hits.append((va + i, nm.rstrip(b'\0').decode(errors='replace')))
        j = i + len(pat)
        while blob[j:j + 4] == dw: j += 4
        i = blob.find(pat, j)
for rva, nm in hits: print(f'{nm:8} {rva:#x}')
data = [(r, n) for r, n in hits if n != '.text']
if not data: sys.exit()
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
lo = min(r for r, _ in data) - 0x10; hi = max(r for r, _ in data) + 0x40
print('-- referencias RIP-relativas:')
# varre funcoes do .pdata (sem .pdata, nao ha referencias listadas)
for b, e in pe.funcs:
    for ins in md.disasm(pe.read(b, e - b), b):
        for op in ins.operands:
            if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP:
                t = ins.address + ins.size + op.mem.disp
                if lo <= t < hi and any(r - 0x10 <= t < r + 0x40 for r, _ in data):
                    print(f'  {ins.address:#x} (func {b:#x}) {ins.mnemonic} {ins.op_str} -> {t:#x}')
