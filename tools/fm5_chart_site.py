"""fm5_chart_site.py EXE : procura o desenhista dos graficos da UI (relacao de marchas; FM6 0xcff4e8) pela forma:
call [rax+0x38] ; ... call [rax+0x30] (SetViewport, rdx = retangulo int) ; ... call [rax+0x58] ; ... call [rax+0x68]
no mesmo objeto. Imprime cada candidato (RVA do call 0x30) com a funcao (.pdata)."""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d
def o2r(o):
    for nm, vs, va, rs, ro in pe.secs:
        if ro <= o < ro + rs: return o - ro + va
pat = re.compile(rb'\xff\x50\x38.{10,60}?\xff\x50\x30.{20,160}?\xff\x50\x58.{5,60}?\xff\x50\x68', re.S)
for m in pat.finditer(d):
    r = o2r(m.start()); c30 = o2r(m.start() + m.group().find(b'\xff\x50\x30', 3))
    f = pe.func(r)
    print('call38 %#x call30 %#x func %s' % (r, c30, f and (hex(f[0]), hex(f[1]))))
