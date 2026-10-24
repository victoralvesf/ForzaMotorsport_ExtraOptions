"""dxbc_compile.py SRC.hlsl ENTRY PROFILE OUT.dxbc - compila com o d3dcompiler_47.dll do Windows (referencia de
encoding DXBC para os testes do proxy); imprime os chunks e o OSGN."""
import ctypes, sys, struct
d3dc = ctypes.WinDLL('d3dcompiler_47.dll')
src = open(sys.argv[1], 'rb').read()
code = ctypes.c_void_p(); err = ctypes.c_void_p()
hr = d3dc.D3DCompile(src, len(src), None, None, None, sys.argv[2].encode(), sys.argv[3].encode(), 0, 0,
                     ctypes.byref(code), ctypes.byref(err))
class Blob(ctypes.Structure): pass
def blob_bytes(p):
    vt = ctypes.cast(ctypes.cast(p, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    getptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])
    getsize = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])
    return ctypes.string_at(getptr(p), getsize(p))
if hr != 0:
    print('hr %#x' % (hr & 0xffffffff), blob_bytes(err).decode(errors='replace') if err else ''); sys.exit(1)
b = blob_bytes(code)
open(sys.argv[4], 'wb').write(b)
n = struct.unpack_from('<I', b, 0x1c)[0]
for k in range(n):
    off = struct.unpack_from('<I', b, 0x20 + 4 * k)[0]
    tag = b[off:off + 4]; sz = struct.unpack_from('<I', b, off + 4)[0]
    print(tag, hex(off), sz)
    if tag in (b'OSGN', b'ISGN'):
        c = b[off + 8:off + 8 + sz]; cnt = struct.unpack_from('<I', c, 0)[0]
        print('  hdr', struct.unpack_from('<II', c, 0))
        for i in range(cnt):
            no, si, sv, ct, reg, m, rw = struct.unpack_from('<IIIIIBB', c, 8 + 24 * i)
            print('  ', c[no:c.find(b'\0', no)], si, sv, ct, hex(reg), hex(m), hex(rw))
    if tag == b'SHEX':
        t = struct.unpack_from('<%dI' % (sz // 4), b, off + 8)
        print('  ', ' '.join('%08x' % x for x in t))
