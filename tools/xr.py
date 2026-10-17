"""xr.py EXE cmd args
  callers RVA...      : E8/E9 rel32 refs to RVA, with containing function
  bytes HEX           : exact byte search in .text (?? = wildcard)
  riprefs RVA...      : RIP-relative refs (lea/mov/call [rip]) to RVA
"""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); cmd = sys.argv[2]; t = pe.text; base = pe.text_rva

def fn(r):
    f = pe.func(r); return f and hex(f[0])

if cmd == 'callers':
    tg = {int(x, 16) for x in sys.argv[3:]}
    for i in range(len(t) - 5):
        if t[i] in (0xe8, 0xe9):
            dst = base + i + 5 + struct.unpack_from('<i', t, i + 1)[0]
            if dst in tg: print(f'{base+i:#x} {"call" if t[i]==0xe8 else "jmp "} {dst:#x} in {fn(base+i)}')
elif cmd == 'bytes':
    h = sys.argv[3].replace(' ', '')
    pat = b''.join(b'.' if h[k:k+2] == '??' else re.escape(bytes.fromhex(h[k:k+2])) for k in range(0, len(h), 2))
    for m in re.finditer(pat, t, re.S): print(f'{base+m.start():#x} in {fn(base+m.start())}')
elif cmd == 'riprefs':
    tg = {int(x, 16) for x in sys.argv[3:]}
    for i in range(len(t) - 4):
        disp = struct.unpack_from('<i', t, i)[0]
        for tail in (0, 1, 4):
            if base + i + 4 + tail + disp in tg and i >= 2 and (t[i-1] & 0xc7) == 5:
                print(f'~{base+i-3:#x} -> {base+i+4+tail+disp:#x} in {fn(base+i)}')
