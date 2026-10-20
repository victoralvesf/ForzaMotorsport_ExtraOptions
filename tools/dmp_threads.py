"""dmp_threads.py DMP [N] - todas as threads de um .dmp: RIP e os N primeiros enderecos de codigo na pilha
   (modulo+rva; game+ para a imagem do jogo em 0x140000000). Padrao N=10."""
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    if 0x140000000 <= v < 0x142600000: return f'game+{v-0x140000000:#x}'
    return None
exc_tid = mf.exception.exception_records[0].ThreadId if mf.exception else None
for t in mf.threads.threads:
    c = t.ContextObject
    if c is None: continue
    out = []
    data = b''
    for size in (0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100):   # a pilha salva pode acabar antes de rsp+0x2000
        try:
            data = r.read(c.Rsp, size); break
        except Exception:
            pass
    for i in range(0, len(data) - 7, 8):
        s = sym(struct.unpack_from('<Q', data, i)[0])
        if s:
            out.append(s)
            if len(out) >= N: break
    mark = ' (excecao)' if t.ThreadId == exc_tid else ''
    print(f'tid {t.ThreadId}{mark} rip {sym(c.Rip) or hex(c.Rip)}: ' + ' '.join(out))
