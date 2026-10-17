# Le o RTL heap failure info (param da excecao 0xc0000374) de um .dmp e lista os modulos.
# Uso: python tools/dmp_heapfail.py <dmp>
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
er = mf.exception.exception_records[0].ExceptionRecord
p = er.ExceptionInformation[0]
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    if 0x140000000 <= v < 0x142600000: return f'game+{v-0x140000000:#x}'
    return ''
try:
    b = r.read(p, 0x100)
    q = struct.unpack_from('<32Q', b)
    for i, v in enumerate(q): print(f'+{i*8:#04x}: {v:#018x} {sym(v)}')
except Exception as ex: print('nao legivel:', ex)
for lo,hi,n in mods:
    if n.lower() in ('azimuth.exe','winmm.dll','ntdll.dll','d3d12core.dll'): print(n, hex(lo))
