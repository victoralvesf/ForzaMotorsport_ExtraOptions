"""dxbc_disasm.py BLOB [BLOB...] - D3DDisassemble (d3dcompiler_47.dll from System32) of DXBC containers,
e.g. the fm6_gs_dump\\*.dxbc written by the proxy with D3D12Debug>=3."""
import ctypes, sys

d3dc = ctypes.WinDLL('d3dcompiler_47.dll')

class ID3DBlob(ctypes.Structure):
    pass

def disasm(data):
    out = ctypes.c_void_p()
    hr = d3dc.D3DDisassemble(data, ctypes.c_size_t(len(data)), 0, None, ctypes.byref(out))
    if hr < 0 or not out.value:
        return 'D3DDisassemble hr=%#x' % (hr & 0xffffffff)
    vt = ctypes.cast(ctypes.cast(out, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(out)
    size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(out)
    text = ctypes.string_at(ptr, size).decode('latin-1').rstrip('\0')
    ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vt[2])(out)
    return text

for path in sys.argv[1:]:
    print('=====', path)
    print(disasm(open(path, 'rb').read()))
