r"""tls_users.py EXE [TLS_OFF...]
Lists code that reads the exe's _tls_index (from the TLS directory) and the TLS-block offsets used
right after it (gs:[0x58] -> [rcx+rax*8] -> [reg+off]). With TLS_OFF, only sites touching those offsets."""
import sys, struct, re, capstone
sys.path.insert(0, __import__('os').path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d
want = {int(x, 16) for x in sys.argv[2:]}
e = struct.unpack_from('<I', d, 0x3c)[0]; o = e + 24
tr = struct.unpack_from('<I', d, o + 112 + 9 * 8)[0]
idx_va = struct.unpack_from('<Q', pe.read(tr, 0x28), 16)[0]
nsec = struct.unpack_from('<H', d, e + 6)[0]; so = o + struct.unpack_from('<H', d, e + 20)[0]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
hits = 0; allo = set()
for i in range(nsec):
    name, vs, va, rs, ra = struct.unpack_from('<8sIIII', d, so + i * 40)
    if not struct.unpack_from('<I', d, so + i * 40 + 36)[0] & 0x20000000: continue
    code = d[ra:ra + rs]
    # rip-relative 32-bit loads: 8b 05/0d/15/1d/... disp32 (with optional REX)
    for m in re.finditer(rb'[\x40-\x4f]?\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', code):
        at = m.start(); n = m.end()
        disp = struct.unpack_from('<i', code, n)[0]
        if 0x140000000 + va + n + 4 + disp != idx_va: continue
        ins = list(md.disasm(code[at:at + 80], 0x140000000 + va + at))[:12]
        offs = set()
        for x in ins[1:8]:   # TLS offsets are SECREL immediates: mov reg, imm or [reg + disp] after the slot load
            mm = re.fullmatch(r'(?:mov|lea) \w+, (0x[0-9a-f]+|\d+)', f'{x.mnemonic} {x.op_str}')
            if mm: offs.add(int(mm.group(1), 0))
        if want and not (offs & want): continue
        hits += 1
        print(hex(ins[0].address), ' | '.join(f'{x.mnemonic} {x.op_str}' for x in ins[:10]))
        allo.update(offs)
print('tls_index', hex(idx_va), 'sites', hits, 'offsets', sorted(hex(x) for x in allo))
