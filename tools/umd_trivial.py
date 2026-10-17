"""umd_trivial.py UMD.dll vt n [names_azimuth_va n map] - slots of a driver vtable whose body is trivial
   (ret / xor eax,eax;ret / mov eax,imm;ret / or eax,-1;ret, also through one jmp); optional Azimuth names,
   with map 'fm6' = FM6 context slot -> Azimuth slot (+3 inserted slots)"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); vt = int(sys.argv[2], 16); n = int(sys.argv[3], 16)
def body(q, depth=0):
    b = bytes(pe.read(q, 16))
    if b[0] == 0xe9 and depth < 2: return body(q + 5 + struct.unpack_from('<i', b, 1)[0], depth + 1)
    if b[0] == 0xc3 or b[:3] == b'\xc2\x00\x00': return 'ret'
    if b[:3] == b'\x33\xc0\xc3': return 'xor eax,eax;ret'
    if b[0] == 0xb8 and b[5] == 0xc3: return 'mov eax,%#x;ret' % struct.unpack_from('<I', b, 1)[0]
    if b[:4] == b'\x83\xc8\xff\xc3': return 'or eax,-1;ret'
    return None
names = {}
if len(sys.argv) > 5:
    az = open(os.path.join(os.path.dirname(__file__), '..', 'azimuth.exe'), 'rb').read()
    tab = int(sys.argv[4], 16) - 0x140001400
    for i in range(int(sys.argv[5], 16)):
        p = struct.unpack_from('<Q', az, tab + 8 * i)[0]
        if p: o = p - 0x140001400; names[i] = az[o:az.index(b'\0', o)].decode('latin1')
fm6 = len(sys.argv) > 6 and sys.argv[6] == 'fm6'
def amap(s):
    if not fm6: return s
    if s < 8: return s
    if s == 8 or s in (0x8e, 0x91): return None   # new: slot 8, PIXBeginEventEx, PIXSetMarkerEx
    return s - 1 if s < 0x8e else s - 2 if s < 0x91 else s - 3
for i in range(n):
    q = struct.unpack_from('<Q', pe.d, pe.r2o(vt + 8 * i))[0] - pe.base
    t = body(q)
    if t:
        a = amap(i)
        print(f'{i:#04x} {q:#8x} {t:20} az {a if a is None else hex(a)} {names.get(a, "") if a is not None else "(novo)"}')
