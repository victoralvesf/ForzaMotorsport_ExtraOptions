"""fm6_texdesc_sites.py EXE - find the game's texture-desc helper (GetType [vt+0x48] then GetDesc [vt+0x68] in the
FM6 layout) and the functions that call it and compare a SampleDesc Quality against 2/3 (MSAA resolve choice).
usage: python tools/fm6_texdesc_sites.py game_binaries/FM6/forza_durango_release_final.exe [gettype_off getdesc_off]
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

pe = PE(sys.argv[1])
gt = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x48
gd = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x68
md = Cs(CS_ARCH_X86, CS_MODE_64)

def insns(f):
    return list(md.disasm(pe.read(f[0], f[1] - f[0]), f[0]))

helpers = []
for f in pe.funcs:
    if f[1] - f[0] > 0x400: continue
    ins = insns(f)
    ops = [i.op_str for i in ins if i.mnemonic == 'call']
    if any(o.endswith(f'+ {gt:#x}]') for o in ops) and sum(o.endswith(f'+ {gd:#x}]') for o in ops) >= 2:
        helpers.append(f[0])
print('helpers:', [hex(h) for h in helpers])
hs = {f'{h:#x}' for h in helpers}
for f in pe.funcs:
    ins = insns(f)
    calls = [i for i in ins if i.mnemonic == 'call' and i.op_str in hs]
    if not calls: continue
    cmps = [i for i in ins if i.mnemonic == 'cmp' and i.op_str.endswith((', 3', ', 2', ', 4'))]
    print(f'{f[0]:#x}-{f[1]:#x} calls {[c.op_str for c in calls]} cmps {len(cmps)}:',
          '; '.join(f'{c.address:#x} {c.op_str}' for c in cmps[:8]))
