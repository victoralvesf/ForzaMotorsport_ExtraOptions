"""strrefs.py EXE FILEOFF... - for strings at file offsets: RVA, absolute qword pointers to it
(data tables) and RIP-relative code refs."""
import sys, os, struct, subprocess
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1])

def o2r(o):
    for nm, vs, va, rs, ro in pe.secs:
        if ro <= o < ro + rs: return o - ro + va

def r2o(r): return pe.r2o(r)

for a in sys.argv[2:]:
    o = int(a, 16); r = o2r(o)
    q = struct.pack('<Q', pe.base + r); hits = []; i = pe.d.find(q)
    while i >= 0: hits.append(hex(o2r(i))); i = pe.d.find(q, i + 1)
    print(f'off {o:#x} rva {r:#x} qword ptrs at rva: {" ".join(hits) or "-"}')
