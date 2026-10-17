"""dispwin.py EXE WIN DISP... - acha trechos do .text (janela WIN bytes) que usam TODOS os deslocamentos disp32
dados (ex.: campos de uma struct: 108 10c 110 114). Heuristica por bytes (modrm com disp32), conferir com xdis."""
import sys, os, re, struct, bisect
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); t = pe.text; base = pe.text_rva; win = int(sys.argv[2])
disps = [int(x, 16) for x in sys.argv[3:]]
pos = {}
for dsp in disps:
    p = struct.pack('<I', dsp); L = []
    i = t.find(p)
    while i >= 0:
        if (t[i-1] & 0xc0) == 0x80: L.append(i)
        i = t.find(p, i + 1)
    pos[dsp] = L
seen = set()
for i in pos[disps[0]]:
    if all(any(abs(j - i) <= win for j in pos[dsp][max(0, bisect.bisect_left(pos[dsp], i - win)):bisect.bisect_left(pos[dsp], i + win + 1)]) for dsp in disps[1:]):
        f = pe.func(base + i); k = f[0] if f else (base + i) & ~0xff
        if k in seen: continue
        seen.add(k); print(f'{base+i:#x} in {f and hex(f[0])}')
