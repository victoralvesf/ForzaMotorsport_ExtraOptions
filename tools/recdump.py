"""recdump.py EXE recVA [len]  - dump game record qwords side by side, describing pointer targets"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); B = pe.base
def desc(v):
    if not (B <= v < B + 0x400000): return ''
    raw = pe.read(v - B, 0x90) or b''
    if len(raw) > 2 and raw[1] == 0 and 32 <= raw[0] < 127:   # utf16
        s = raw.decode('utf-16le', 'replace').split('\0')[0]; return 'L"%s"' % s
    s = raw.split(b'\0')[0]
    if len(s) >= 2 and all(32 <= c < 127 for c in s): return '"%s"' % s.decode()
    return '-> ' + ' '.join('%016x' % struct.unpack_from('<Q', raw, i)[0] for i in range(0, 32, 8))
def dump(rec, n):
    out = []
    for o in range(0, n, 8):
        v = struct.unpack_from('<Q', pe.read(rec - B + o, 8))[0]
        out.append((o, v, desc(v)))
    return out
if __name__ == '__main__':
    rec = int(sys.argv[2], 16); n = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x340
    for o, v, d in dump(rec, n): print(f'+{o:#05x} {v:016x} {d}')
