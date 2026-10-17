"""umd_vtdiff.py A.dll vtA nA B.dll vtB nB [names_azimuth_va n] - aligns two driver vtables slot by slot
   (fingerprint = function size from .pdata, or first bytes for leaf functions) and prints the slot map A->B.
   Optional: Azimuth name table (VA in azimuth.exe, layout of A) to label the slots."""
import sys, os, struct, difflib
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
def load(p, vt, n):
    pe = PE(p); out = []
    for i in range(n):
        q = struct.unpack_from('<Q', pe.d, pe.r2o(vt + 8 * i))[0] - pe.base
        f = pe.func(q); x = pe.r2o(q)
        fp = ('s', f[1] - f[0]) if f and f[0] == q else ('b', pe.d[x:x + 12].hex())
        out.append((q, fp))
    return out
a = load(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16))
b = load(sys.argv[4], int(sys.argv[5], 16), int(sys.argv[6], 16))
names = {}
if len(sys.argv) > 8:
    az = open(os.path.join(os.path.dirname(__file__), '..', 'azimuth.exe'), 'rb').read()
    tab = int(sys.argv[7], 16) - 0x140001400
    for i in range(int(sys.argv[8], 16)):
        p = struct.unpack_from('<Q', az, tab + 8 * i)[0]
        if p: o = p - 0x140001400; names[i] = az[o:az.index(b'\0', o)].decode('latin1')
sm = difflib.SequenceMatcher(None, [x[1] for x in a], [x[1] for x in b], autojunk=False)
for op, i1, i2, j1, j2 in sm.get_opcodes():
    for k in range(max(i2 - i1, j2 - j1)):
        i = i1 + k if i1 + k < i2 else None; j = j1 + k if j1 + k < j2 else None
        fa = f'{i:#04x} {a[i][0]:#7x} {str(a[i][1][1])[:12]:>12}' if i is not None else ' ' * 29
        fb = f'{j:#04x} {b[j][0]:#7x} {str(b[j][1][1])[:12]:>12}' if j is not None else ' ' * 29
        print(f'{op[:3]} {fa} | {fb}  {names.get(i, "") if i is not None else ""}')
