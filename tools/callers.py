"""callers.py DLL 0xRVA [...] - chamadas diretas (call/jmp rel32) e ponteiros (dword RVA / qword VA em dados) para cada RVA."""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1])
for t in [int(x, 16) for x in sys.argv[2:]]:
    print(f'== {t:#x}')
    for m in re.finditer(rb'[\xe8\xe9]', pe.text):
        i = m.start()
        if i + 5 > len(pe.text): break
        if pe.text_rva + i + 5 + struct.unpack_from('<i', pe.text, i + 1)[0] == t:
            r = pe.text_rva + i; f = pe.func(r)
            print(f'  {"call" if pe.text[i] == 0xe8 else "jmp "} {r:#x} (func {f and hex(f[0])})')
    q = struct.pack('<Q', pe.base + t)
    for nm, vs, va, rs, ro in pe.secs:
        blob = pe.d[ro:ro + rs]; i = blob.find(q)
        while i >= 0: print(f'  ptr  {nm.rstrip(bytes(1)).decode()} {va + i:#x}'); i = blob.find(q, i + 1)
