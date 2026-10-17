"""gfx_sites.py - sites graficos do apply_patches no FH2 (resolucao/UI, UI clock, presets de qualidade).
Le o registro do FH2 no azimuth.exe, desmonta os bytes originais no exe do FH2 e lista os valores que o
Azimuth escreve nos presets (tabela 0x14029cee0). Rodar da raiz do projeto."""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
az = open('azimuth.exe', 'rb').read()
R1 = 0x1402a1eb0                                  # registro do FH2 1.0.0.64
def o(va): return va - 0x140001400                # .rdata do azimuth.exe
def I(va): return struct.unpack_from('<I', az, o(va))[0]
class PE:
    def __init__(s, p):
        d = s.d = open(p, 'rb').read()
        e = struct.unpack_from('<I', d, 0x3c)[0]; n = struct.unpack_from('<H', d, e + 6)[0]
        so = struct.unpack_from('<H', d, e + 20)[0]
        s.secs = [struct.unpack_from('<8sIIII', d, e + 24 + so + i * 40) for i in range(n)]
    def read(s, rva, n):
        for nm, vs, va, rs, ro in s.secs:
            if va <= rva < va + rs: return s.d[rva - va + ro:rva - va + ro + n]
fh = PE('game_binaries/FH2/forza_durango_release_final.exe')
md = Cs(CS_ARCH_X86, CS_MODE_64)
OPT = ['shadows', 'reflections', 'car_detail', 'particles', 'motion_blur', 'texture_filtering']
LVL = {0: 'low', 1: 'default', 2: 'high', 3: 'ultra'}
NAMES = {0xa8: 'synchronised update step branch', 0xb8: 'synchronised update step load', 0xd0: 'UI step',
         0xe0: 'UI clock tick', 0xf0: '1/60 s constant', 0x100: 'default vertical resolution store',
         0x110: 'render width from the height', 0x120: 'UI size from the display',
         0x130: "UI cameras' aspect ratio from the UI's size", 0x140: 'UI projection setter',
         0x150: "UI camera's field of view", 0x160: "UI element's projection with a scene camera",
         0x170: "UI element's projection with the UI's camera", 0x180: "UI renderer's matrix function's end",
         0x190: "UI fill's vertex block function", 0x1a8: "start-up screens' rectangle",
         0x1b8: "vignette's aspect ratio", 0x1c8: '16/9 constant'}
LEN = {0xa8: 2, 0xb8: 8, 0xd0: 0x16, 0xe0: 0x124, 0xf0: 4, 0x100: 10, 0x110: 0x15, 0x120: 0x1b, 0x130: 0x3c,
       0x140: 0x61, 0x150: 4, 0x160: 0x1d, 0x170: 0x29, 0x180: 0x1a, 0x190: 0x1f, 0x1a8: 0x22, 0x1b8: 0x15, 0x1c8: 4}

def dis(rva, n, maxi=12):
    b = fh.read(rva, n); out = []
    for k, ins in enumerate(md.disasm(b, 0x140000000 + rva)):
        if k >= maxi: out.append('      ...'); break
        out.append('      %x  %-24s %s %s' % (ins.address - 0x140000000, ins.bytes.hex(), ins.mnemonic, ins.op_str))
    return '\n'.join(out)

print('== sites de resolucao/aspecto/UI e UI clock (registro FH2 %#x) ==' % R1)
for off in sorted(NAMES):
    rva = I(R1 + off); n = LEN[off]
    print('+%#x  %-45s FH2 RVA %#x, %d bytes' % (off, NAMES[off], rva, n))
    if n <= 0x40 and off not in (0xf0, 0x150, 0x1c8): print(dis(rva, n))
    elif off in (0xf0, 0x150, 0x1c8): print('      valor: %r' % struct.unpack('<f', fh.read(rva, 4)))

print('\n== presets de qualidade (tabela 0x14029cee0, 16 entradas de 0x30) ==')
sites = {}
for i in range(16):
    e = 0x14029cee0 + i * 0x30
    opt, lvl, off = struct.unpack_from('<QII', az, o(e))
    cnt = I(e + 0x10); vals = struct.unpack_from('<%dI' % 5, az, o(e + 0x14))
    rec = 0xa8 + off; rva = I(R1 + rec)
    sites.setdefault(rec, []).append((OPT[opt], LVL[lvl], cnt, vals))
for rec in sorted(sites):
    rva = I(R1 + rec); cnt = sites[rec][0][2]
    print('+%#x  FH2 RVA %#x (%s), %d dwords escritos' % (rec, rva, sites[rec][0][0], cnt))
    print('    original: %s' % ' '.join('%#x/%g' % (v, struct.unpack('<f', struct.pack('<I', v))[0])
                                     for v in struct.unpack('<%dI' % cnt, fh.read(rva, cnt * 4))))
    for opt, lvl, c, vals in sites[rec]:
        print('    %-8s %s' % (lvl, ' '.join('%#x/%g' % (v, struct.unpack('<f', struct.pack('<I', v))[0]) for v in vals[:c])))
    print(dis(rva - 0x10, 0x30, 8))
