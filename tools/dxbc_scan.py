"""dxbc_scan.py FILE - finds DXBC containers in any file (e.g. a full .dmp), checks their hashes and counts the
opcodes above 0xd5 (Xbox-only extensions) per opcode, with the instruction length and an example."""
import os, struct, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(__file__))
from dxbc import checksum, chunks, LAST_PC_OPCODE, CUSTOMDATA

d = open(sys.argv[1], 'rb').read()
seen, hashes, xb, ex = set(), Counter(), Counter(), defaultdict(set)
pos = d.find(b'DXBC')
while pos >= 0:
    total = struct.unpack_from('<I', d, pos + 0x18)[0] if pos + 0x20 <= len(d) else 0
    blob = d[pos:pos + total]
    if 0x20 < total < 0x200000 and len(blob) == total and blob[4:20] not in seen:
        seen.add(blob[4:20])
        tail = (total - 20) % 64 >= 56
        hashes[('ok' if checksum(blob) == blob[4:20] else 'bad', tail)] += 1
        try:
            for tag, o, size in chunks(blob):
                if tag not in ('SHEX', 'SHDR'): continue
                code = blob[o:o + size]; ndw = struct.unpack_from('<I', code, 4)[0]; i = 2
                while i < ndw and (i + 1) * 4 <= len(code):
                    tok = struct.unpack_from('<I', code, i * 4)[0]; op = tok & 0x7ff
                    ln = struct.unpack_from('<I', code, i * 4 + 4)[0] if op == CUSTOMDATA else tok >> 24 & 0x7f
                    if ln == 0: break
                    if op > LAST_PC_OPCODE:
                        xb[op] += 1
                        if len(ex[op]) < 3:
                            ex[op].add(' '.join('%08x' % w for w in struct.unpack_from('<%dI' % ln, code, i * 4)))
                    i += ln
        except Exception:
            hashes['unparsed'] += 1
    pos = d.find(b'DXBC', pos + 4)
print('containers (hash, tail>=56):', dict(hashes))
for op, n in sorted(xb.items()):
    print(f'opcode {op:#x}: {n}')
    for e in sorted(ex[op]): print('   ', e)
