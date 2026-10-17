"""outcalls.py EXE disp [disp...] - sites 'lea rdx,[rsp+X]; ...; call [vt+disp]' e como [rsp+X] e usado depois
   (qword -> objeto devolvido, ex. GetResource/GetDevice; dword -> valor, ex. GetType). Agrupa por disp."""
import sys, os, collections
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); disps = [int(a, 16) for a in sys.argv[2:]]
md = Cs(CS_ARCH_X86, CS_MODE_64)
res = collections.defaultdict(collections.Counter); ex = collections.defaultdict(list)
LO, HI = (int(os.environ.get("LO", "0"), 16), int(os.environ.get("HI", "ffffffff"), 16))
for b, e in pe.funcs:
    if not (LO <= b < HI): continue
    if not (pe.text_rva <= b < pe.text_rva + pe.text_len): continue
    ins = list(md.disasm_lite(pe.read(b, e - b), b))
    for i, (a, s, m, op) in enumerate(ins):
        if m != 'call' or not op.startswith('qword ptr [r') or '+ 0x' not in op: continue
        try: d = int(op.split('+ ')[1].rstrip(']'), 16)
        except ValueError: continue
        if d not in disps: continue
        slot = None
        for j in range(i - 1, max(i - 5, -1), -1):
            if ins[j][2] == 'lea' and ins[j][3].startswith('rdx, [rsp'):
                slot = ins[j][3].split(', ')[1]; break
        if not slot: continue
        kind = '?'
        for a2, s2, m2, op2 in ins[i + 1:i + 8]:
            if slot in op2 and m2 in ('mov', 'movzx', 'cmp', 'test'):
                kind = op2.split(slot)[0].split(',')[-1].strip().split(' ')[0] if ',' in op2 else op2.split()[0]
                kind = 'qword' if 'qword' in op2 else 'dword' if 'dword' in op2 else 'byte' if 'byte' in op2 else op2
                break
        res[d][kind] += 1; ex[(d, kind)].append(a)
for d in disps:
    print(hex(d), dict(res[d]), {k: [hex(x) for x in ex[(d, k)][:8]] for k in res[d]})
