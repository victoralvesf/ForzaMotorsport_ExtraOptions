import sys, re
sys.path.insert(0, r'E:\Azimuth For FM5\tools')
from sigfind import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
pe = PE(sys.argv[1]); lo = int(sys.argv[2],16); hi = int(sys.argv[3],16)
md = Cs(CS_ARCH_X86, CS_MODE_64)
prev = []
a = lo
while a < hi:
    f = pe.func(a)
    if not f: a += 1; continue
    ins = list(md.disasm_lite(pe.read(f[0], f[1]-f[0]), f[0]))
    for i,(ad,s,m,op) in enumerate(ins):
        if m == 'call' and re.match(r'qword ptr \[r\w+ \+ 0x(18|20|28|30|38|40)\]', op):
            ctx = '; '.join(f'{x[2]} {x[3]}' for x in ins[max(0,i-4):i])
            print(f'{f[0]:#x} {ad:#x} {op}  <- {ctx}')
    a = f[1]
