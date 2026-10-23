"""fm6_dynrender_getter.py EXE ID [ID...] - acha o getter de um controle dos dynamic render settings do FM6 pelo id
(tools\fm5_dynrender.py lista os ids e serve no FM6) e os chamadores (call rel32). No FM6 o teste do bit e por byte:
  test byte [rcx+0x88+id/8], 1<<(id%8)   (f6 81 <disp32> <mask>)   ou   test dword [rcx+0x88+4*(id/32)], 1<<(id%32)
e nas camadas test byte [reg+0x30+id/8], mask. Usado no car_detail do FM6 (ids 9..0xd, 0x11..0x13)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d; t = pe.text; base = pe.text_rva
def rva(o):
    for nm, vs, va, rs, ro in pe.secs:
        if ro <= o < ro + rs: return va + o - ro
for a in sys.argv[2:]:
    i = int(a, 0)
    pats = [b'\xf6\x81' + struct.pack('<I', 0x88 + i // 8) + bytes([1 << (i % 8)]),
            b'\xf7\x81' + struct.pack('<I', 0x88 + 4 * (i // 32)) + struct.pack('<I', 1 << (i % 32))]
    for pat in pats:
        for m in re.finditer(re.escape(pat), d):
            r = rva(m.start()); f = pe.func(r)
            if not f: print(f'id {i:#x}: uso {r:#x} sem funcao'); continue
            calls = [base + k for k in range(len(t) - 5)
                     if t[k] == 0xe8 and base + k + 5 + struct.unpack_from('<i', t, k + 1)[0] == f[0]]
            print(f'id {i:#x}: getter {f[0]:#x} (teste {r:#x}) prologo {pe.read(f[0], 20).hex()} '
                  f'chamadores {[hex(c) for c in calls]}')
