"""Le um minidump do kernel (BSOD, C:\\Windows\\Minidump\\*.dmp, PAGEDU64 tipo 4 = triage) sem WinDbg:
bugcheck, contexto da excecao, drivers carregados e a pilha do kernel (enderecos que caem em drivers).
Uso: python tools/kdmp_triage.py <arquivo.dmp>"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'PAGEDU64', 'nao e dump do kernel de 64 bits'
code, = struct.unpack_from('<I', d, 0x38)
params = struct.unpack_from('<4Q', d, 0x40)
print('bugcheck %#x (%s)' % (code, ', '.join('%#x' % p for p in params)))

T = 0x2000
(sp_build, size, valid, ctx_off, exc_off, mm_off, unl_off, prcb_off, proc_off, thr_off, cs_off, cs_size,
 drv_off, drv_n, str_off, str_size, broken_off, opts) = struct.unpack_from('<18I', d, T)
top, = struct.unpack_from('<Q', d, T + 0x48)

def name_at(off):
    n, = struct.unpack_from('<I', d, off)
    return d[off + 4: off + 4 + n * 2].decode('utf-16le', 'replace')

# DUMP_DRIVER_ENTRY64: nome (offset) + KLDR_DATA_TABLE_ENTRY64 (DllBase em +0x30, SizeOfImage em +0x40)
drivers = []
stride = 0
for s in (0xa8, 0xa0, 0xb0, 0xb8, 0x98, 0x90, 0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0):
    ok = True
    for i in range(min(drv_n, 6)):
        e = drv_off + i * s
        no, = struct.unpack_from('<I', d, e)
        base, = struct.unpack_from('<Q', d, e + 8 + 0x30)
        if not (str_off <= no < str_off + str_size) or base >> 48 != 0xffff:
            ok = False
            break
    if ok:
        stride = s
        break
assert stride, 'layout da lista de drivers nao reconhecido'
for i in range(drv_n):
    e = drv_off + i * stride
    no, = struct.unpack_from('<I', d, e)
    base, = struct.unpack_from('<Q', d, e + 8 + 0x30)
    sz, = struct.unpack_from('<I', d, e + 8 + 0x40)
    drivers.append((base, sz, name_at(no)))

def where(a):
    for base, sz, n in drivers:
        if base <= a < base + sz:
            return '%s+%#x' % (n, a - base)
    return None

print('falha em', where(params[1]) or hex(params[1]))
# CONTEXT x64: Rip em +0xf8, Rsp em +0x98
rip, = struct.unpack_from('<Q', d, ctx_off + 0xf8)
rsp, = struct.unpack_from('<Q', d, ctx_off + 0x98)
print('contexto: rip %#x (%s) rsp %#x' % (rip, where(rip), rsp))
if exc_off:
    ecode, eflags, erec, eaddr, enp = struct.unpack_from('<IIQQI', d, exc_off)
    info = struct.unpack_from('<%dQ' % min(enp, 4), d, exc_off + 0x20)
    print('excecao %#x em %#x (%s) info %s' % (ecode, eaddr, where(eaddr), [hex(x) for x in info]))
broken = [n for b, s, n in drivers if 'amd' in n.lower() or 'dxg' in n.lower() or 'nv' in n.lower()[:2]]
print('drivers de video:', ', '.join(broken))
print('pilha (top %#x, %d bytes):' % (top, cs_size))
seen = 0
for i in range(0, cs_size - 7, 8):
    v, = struct.unpack_from('<Q', d, cs_off + i)
    w = where(v)
    if w:
        print('  rsp+%#06x %#x %s' % (i, v, w))
        seen += 1
        if seen > 60:
            break
