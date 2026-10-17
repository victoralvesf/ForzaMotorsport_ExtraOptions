# Le qwords da memoria de um .dmp. Uso: python tools/dmp_read.py <dmp> <va|mod+rva> [n]
#   ex.: azimuth.exe+0x37d1e0 16
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
mods = [(m.baseaddress, m.baseaddress+m.size, m.name.split(chr(92))[-1]) for m in mf.modules.modules]
def sym(v):
    for lo,hi,n in mods:
        if lo <= v < hi: return f'{n}+{v-lo:#x}'
    if 0x140000000 <= v < 0x142600000: return f'game+{v-0x140000000:#x}'
    return ''
a = sys.argv[2]
if '+' in a:
    mn, off = a.split('+'); a = next(lo for lo,hi,n in mods if n.lower() == mn.lower()) + int(off, 0)
else: a = int(a, 0)
n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 8
b = r.read(a, n*8)
for i, v in enumerate(struct.unpack_from(f'<{n}Q', b)): print(f'{a+i*8:#x} [{i:#x}] {v:#018x} {sym(v)}')
