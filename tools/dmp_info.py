# Resumo de um .dmp do Azimuth: registro de excecao + varredura da pilha da thread por enderecos do
# exe do jogo (0x140000000..) e de modulos. Uso: python tools/dmp_info.py <dmp> [bytes_pilha]
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
size = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x3000
e = mf.exception.exception_records[0]; er = e.ExceptionRecord
print('tid', e.ThreadId, 'code', er.ExceptionCode, 'addr', hex(er.ExceptionAddress), 'flags', er.ExceptionFlags)
print('params', [hex(p) for p in er.ExceptionInformation[:er.NumberParameters]])
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    if 0x140000000 <= v < 0x142600000: return f'game+{v-0x140000000:#x}'
    return None
ctx = next(t.ContextObject for t in mf.threads.threads if t.ThreadId == e.ThreadId)
print('thread rip', hex(ctx.Rip), sym(ctx.Rip), 'rsp', hex(ctx.Rsp))
sp = ctx.Rsp
data = b''
while len(data) < size:
    try: data += r.read(sp + len(data), 0x100)
    except Exception: break
for i in range(0, len(data) - 7, 8):
    v = struct.unpack_from('<Q', data, i)[0]
    s = sym(v)
    if s: print(f'rsp+{i:#x} ({sp+i:#x}): {v:#x} {s}')
