# Conta a tabela de PSOs do gpuguard (pso_tab, common/gpuguard.c) num .dmp e procura PSOs.
# Uso: python tools/dmp_pso_tab.py <dmp> <rva de pso_tab no winmm.dll (nm: endereco - ImageBase)> [slots] [pso ...]
import sys, struct, logging
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile
mf = MinidumpFile.parse(sys.argv[1]); r = mf.get_reader()
base = next(m.baseaddress for m in mf.modules.modules if m.name.split(chr(92))[-1].lower() == 'winmm.dll')
rva = int(sys.argv[2], 0)
slots = int(sys.argv[3], 0) if len(sys.argv) > 3 else 8192
want = {int(p, 16) for p in sys.argv[4:]}
esz = 32   # void *pso, *rs; uint32_t slots, nelem, hash, info
b = r.read(base + rva, slots * esz)
used = 0; gs = 0; rss = {}
for i in range(slots):
    pso, rs, sl, ne, h, info = struct.unpack_from('<QQIIII', b, i * esz)
    if not pso: continue
    used += 1
    if info & 0x10: gs += 1
    rss[rs] = rss.get(rs, 0) + 1
    if pso in want: print(f'found {pso:#x} slot {i} rs {rs:#x} hash {h:08x} info {info:#x}')
print(f'pso_tab: {used} of {slots} used, {gs} with GS')
for rs, c in sorted(rss.items(), key=lambda x: -x[1])[:8]: print(f'  rs {rs:#x}: {c}')
