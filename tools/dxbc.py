"""dxbc.py BLOB [BLOB...] - lists the DXBC chunks, checks the container hash and walks SHEX/SHDR: version,
opcode histogram and any opcode above the last one the PC runtime knows (D3D11.1, 0xd5) or with a bad length.
-v prints every instruction (offset, opcode, length)."""
import math, struct, sys
from collections import Counter

LAST_PC_OPCODE = 0xd5          # last SM5 opcode the PC parser accepts
CUSTOMDATA = 0x35

def _md5_transform(st, blk):
    K = [int(abs(math.sin(i + 1)) * 2**32) & 0xffffffff for i in range(64)]
    R = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
    X = struct.unpack('<16I', blk)
    a, b, c, d = st
    for i in range(64):
        if i < 16: f, g = (b & c) | (~b & d), i
        elif i < 32: f, g = (d & b) | (~d & c), (5 * i + 1) % 16
        elif i < 48: f, g = b ^ c ^ d, (3 * i + 5) % 16
        else: f, g = c ^ (b | (~d & 0xffffffff)), 7 * i % 16
        f = (f + a + K[i] + X[g]) & 0xffffffff
        a, d, c = d, c, b
        b = (b + ((f << R[i]) | (f >> (32 - R[i])))) & 0xffffffff
    return [(x + y) & 0xffffffff for x, y in zip(st, (a, b, c, d))]

def checksum(d):
    """DXBC hash: MD5 of the bytes after the hash field, with the container's own final block
    (bit count first, (bits >> 2) | 1 last)."""
    m = d[20:]; n = len(m); st = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
    full = n & ~63
    for i in range(0, full, 64): st = _md5_transform(st, m[i:i + 64])
    rest = m[full:]; bits = n * 8
    if len(rest) >= 56:
        st = _md5_transform(st, (rest + b'\x80').ljust(64, b'\0'))
        blk = struct.pack('<I', bits) + b'\0' * 56 + struct.pack('<I', (bits >> 2) | 1)
    else:
        blk = (struct.pack('<I', bits) + rest + b'\x80').ljust(60, b'\0') + struct.pack('<I', (bits >> 2) | 1)
    st = _md5_transform(st, blk)
    return struct.pack('<4I', *st)

def chunks(d):
    assert d[:4] == b'DXBC', 'not DXBC'
    total, n = struct.unpack_from('<II', d, 0x18)
    offs = struct.unpack_from('<%dI' % n, d, 0x20)
    for o in offs:
        tag, size = d[o:o + 4], struct.unpack_from('<I', d, o + 4)[0]
        yield tag.decode('latin-1'), o + 8, size

def walk(code, verbose):
    ver, ndw = struct.unpack_from('<II', code, 0)
    kind = {0: 'ps', 1: 'vs', 2: 'gs', 3: 'hs', 4: 'ds', 5: 'cs'}.get(ver >> 16, hex(ver >> 16))
    print(f'    {kind}_{ver >> 4 & 0xf}_{ver & 0xf}, {ndw} dwords')
    hist, bad = Counter(), []
    i = 2
    while i < ndw:
        tok = struct.unpack_from('<I', code, i * 4)[0]
        op = tok & 0x7ff
        ln = struct.unpack_from('<I', code, i * 4 + 4)[0] if op == CUSTOMDATA else tok >> 24 & 0x7f
        hist[op] += 1
        if verbose: print(f'      {i * 4:#07x} op {op:#05x} len {ln} tok {tok:#010x}')
        if op > LAST_PC_OPCODE or ln == 0 or i + ln > ndw:
            bad.append((i * 4, op, ln, tok))
            if ln == 0 or i + ln > ndw: break
        i += ln
    print('    opcodes:', ' '.join(f'{k:#x}x{v}' for k, v in sorted(hist.items())))
    for o, op, ln, tok in bad:
        print(f'    !! at {o:#x}: opcode {op:#x} len {ln} tok {tok:#010x}')

if __name__ == '__main__':
    verbose = '-v' in sys.argv
    for f in [a for a in sys.argv[1:] if a != '-v']:
        d = open(f, 'rb').read()
        ok = 'ok' if checksum(d) == d[4:20] else f'BAD (stored {d[4:20].hex()}, computed {checksum(d).hex()})'
        print(f'{f}: {len(d)} bytes, hash {ok}')
        for tag, o, size in chunks(d):
            print(f'  {tag} @{o:#x} {size:#x}')
            if tag in ('SHEX', 'SHDR'): walk(d[o:o + size], verbose)
