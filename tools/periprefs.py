"""periprefs.py PE rva... - rip-relative lea/mov sites (48/4c 8d|8b modrm=rip) in .text whose target is one of the RVAs"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); tg = {int(x, 16) for x in sys.argv[2:]}
code = pe.read(pe.text_rva, pe.text_len)
for i in range(len(code) - 7):
    if code[i] in (0x48, 0x4c) and code[i + 1] in (0x8d, 0x8b) and (code[i + 2] & 0xc7) == 5:
        t = pe.text_rva + i + 7 + struct.unpack_from('<i', code, i + 3)[0]
        if t in tg:
            f = pe.func(pe.text_rva + i)
            print(f'{pe.text_rva + i:#x} -> {t:#x} (func {f and hex(f[0])})')
