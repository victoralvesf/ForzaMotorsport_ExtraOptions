"""leapair.py EXE WIN DISP_A DISP_B - acha 'lea reg,[reg+A]' e 'lea reg,[reg+B]' (disp32) a ate WIN bytes um do
outro (ex.: escolha entre dois controles de uma struct: e0 = CarFocusLODMinMax, f0 = CarNonFocusLODMinMax)."""
import sys, os, re, struct, bisect
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); t = pe.text; base = pe.text_rva; win = int(sys.argv[2])
A, B = int(sys.argv[3], 16), int(sys.argv[4], 16)
def leas(dsp):
    return [m.start() for m in re.finditer(rb'[\x48\x4c\x49\x4d]\x8d[\x80-\xbf]' + re.escape(struct.pack('<I', dsp)), t, re.S)
            if (t[m.start() + 2] & 7) != 4]
la, lb = leas(A), leas(B)
for i in la:
    k = bisect.bisect_left(lb, i - win)
    if k < len(lb) and abs(lb[k] - i) <= win:
        f = pe.func(base + i); print(f'{base+i:#x} / {base+lb[k]:#x} in {f and hex(f[0])}')
