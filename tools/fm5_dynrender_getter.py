"""fm5_dynrender_getter.py EXE ID [ID...] - acha o getter de um controle dos dynamic render settings do FM5 pelo id
(fm5_dynrender.py lista os ids) e os seus chamadores (call rel32). Padrao do teste do bit do id no gerenciador:
  id < 32 : mov eax,[rcx+0x88] ; shr eax,id      ; test al,1
  id >= 32: mov eax,[rcx+0x8c] ; shr eax,id-32   ; test al,1
Usado no car_detail (ids 9..0xc) e no shadows (0x2e ShadowMapResolution, 0x2f MaxHiDefShadows)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d; t = pe.text; base = pe.text_rva
def rva(o):
    for nm, vs, va, rs, ro in pe.secs:
        if ro <= o < ro + rs: return va + o - ro
for a in sys.argv[2:]:
    i = int(a, 0); off, sh = (0x88, i) if i < 32 else (0x8c, i - 32)
    pat = struct.pack('<I', off) + bytes([0xc1, 0xe8, sh, 0xa8, 0x01])
    for m in re.finditer(re.escape(b'\x8b\x81' + pat), d):
        f = pe.func(rva(m.start()))
        if not f: continue
        calls = [base + k for k in range(len(t) - 5)
                 if t[k] == 0xe8 and base + k + 5 + struct.unpack_from('<i', t, k + 1)[0] == f[0]]
        print(f'id {i:#x}: getter {f[0]:#x} prologo {pe.read(f[0], 19).hex()} chamadores {[hex(c) for c in calls]}')
