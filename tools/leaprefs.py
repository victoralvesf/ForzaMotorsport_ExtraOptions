"""leaprefs.py EXE 0xRVA [...] - lea reg,[rip+disp32] referencing each RVA (string/data), with containing function."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1])
targets = {int(x, 16) for x in sys.argv[2:]}
t = pe.text
for i in range(len(t) - 7):
    if t[i] in (0x48, 0x4c) and t[i + 1] == 0x8d and (t[i + 2] & 0xc7) == 0x05:
        r = pe.text_rva + i
        va = r + 7 + struct.unpack_from('<i', t, i + 3)[0]
        if va in targets:
            f = pe.func(r)
            print(f'{r:#x} -> {va:#x} (func {f and hex(f[0])})')
