"""disp_pair.py EXE dispA dispB [dispC ...] - funcoes (.pdata) que tem 'call qword ptr [reg + disp]' para TODAS
   as disps dadas (ex.: Dispatch + CopySubresourceRegion na tabela embutida do contexto); imprime os sites."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE

pe = PE(sys.argv[1])
disps = [int(x, 16) for x in sys.argv[2:]]
data = pe.text
# ff /2 com modrm mod=10 (disp32), reg base != rsp/rbp; com prefixo REX.B (41) para r8..r15
hits = {d: [] for d in disps}
for d in disps:
    tail = struct.pack('<I', d)
    for m in re.finditer(re.escape(b'\xff') + b'[\x90-\x93\x95-\x97]' + re.escape(tail), data):
        off = m.start()
        hits[d].append(pe.text_rva + off - (1 if data[off - 1] == 0x41 else 0))
funcs = {}
for d, rvas in hits.items():
    for r in rvas:
        f = pe.func(r)
        if f:
            funcs.setdefault(f, {}).setdefault(d, []).append(r)
for f, m in sorted(funcs.items()):
    if all(d in m for d in disps):
        print(f'func {f[0]:#x}-{f[1]:#x}: ' + '; '.join(f'{d:#x}@' + ','.join(f'{r:#x}' for r in m[d]) for d in disps))
