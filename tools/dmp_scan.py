"""dmp_scan.py DMP [W H] - thread da excecao: enderecos azimuth.exe/jogo (game+, imagem 0x140000000) na pilha e
   descritores de textura WxH (padrao 1920x1080: 12 dwords a partir do Width)."""
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
tid = mf.exception.exception_records[0].ThreadId
ctx = [t for t in mf.threads.threads if t.ThreadId == tid][0].ContextObject
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
W, H = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (1920, 1080)
n = 0x6000
while n:
    try: data = r.read(ctx.Rsp, n); break
    except Exception: n -= 0x400
for i in range(0, len(data)-0x30, 4):
    w = struct.unpack_from('<12I', data, i)
    if w[0] == W and w[1] == H:
        print(f'rsp+{i:#x} ({ctx.Rsp+i:#x}):', ' '.join(f'{x:#x}' for x in w))
for i in range(0, len(data), 8):
    v = struct.unpack_from('<Q', data, i)[0]; s = sym(v)
    if not s and 0x140001000 <= v < 0x143000000: s = f'game+{v - 0x140000000:#x}'
    if s and ('azimuth' in s or s.startswith('game+')): print(f'rsp+{i:#x}: {s}')
print('read', hex(n))
