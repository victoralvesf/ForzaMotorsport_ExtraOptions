r"""tls_slot0.py EXE [-v] [-rva]  - classify every gs:[0x58] (TEB.ThreadLocalStoragePointer) load by its first use:
  slot0  = [T] / [T+0]  (exe-only TLS model: the block of the module with TLS index 0)
  index  = [T+R*8]      (uses _tls_index)
  other  = anything else (printed with -v)
-rva prints the RVAs of the slot0 loads (sites whose T is used again after [T] are reported as slot0-reused).
Follows the register T through the next instructions until it is used, overwritten or a branch is hit."""
import sys, struct, re, collections, capstone
from capstone import x86
sys.path.insert(0, __import__('os').path.dirname(__file__))
from sigfind import PE
pe = PE(sys.argv[1]); d = pe.d; verbose = '-v' in sys.argv
e = struct.unpack_from('<I', d, 0x3c)[0]; o = e + 24
nsec = struct.unpack_from('<H', d, e + 6)[0]; so = o + struct.unpack_from('<H', d, e + 20)[0]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
pat = re.compile(rb'\x65[\x48\x4c]\x8b[\x04\x0c\x14\x1c\x24\x2c\x34\x3c]\x25\x58\x00\x00\x00')
res = collections.Counter(); slot0 = []
FAM = {}
for q in 'abcd': FAM.update({f'r{q}x': f'r{q}x', f'e{q}x': f'r{q}x', f'{q}x': f'r{q}x', f'{q}l': f'r{q}x', f'{q}h': f'r{q}x'})
for q in ('si', 'di', 'bp', 'sp'): FAM.update({'r' + q: 'r' + q, 'e' + q: 'r' + q, q: 'r' + q, q + 'l': 'r' + q})
for n in range(8, 16): FAM.update({f'r{n}{x}': f'r{n}' for x in ('', 'd', 'w', 'b')})
def fam(regs): return {FAM.get(md.reg_name(r), md.reg_name(r)) for r in regs}
for i in range(nsec):
    name, vs, va, rs, ra = struct.unpack_from('<8sIIII', d, so + i * 40)
    if not struct.unpack_from('<I', d, so + i * 40 + 36)[0] & 0x20000000: continue
    code = d[ra:ra + rs]
    for m in pat.finditer(code):
        ins = list(md.disasm(code[m.start():m.start() + 120], 0x140000000 + va + m.start()))
        if not ins: continue
        t = ins[0].operands[0].reg; tn = md.reg_name(t); kind = 'other'; use = None
        for x in ins[1:12]:
            mems = [op.mem for op in x.operands if op.type == x86.X86_OP_MEM]
            if any(mm.base == t for mm in mems):
                mm = next(mm for mm in mems if mm.base == t)
                kind = 'slot0' if mm.index == 0 and mm.disp == 0 else 'index' if mm.index and mm.scale == 8 and mm.disp == 0 else 'other'
                use = x; break
            r, w = x.regs_access()
            if t in r: use = x; break
            if t in w: kind = 'dead'; use = x; break
            if x.group(x86.X86_GRP_JUMP) or x.group(x86.X86_GRP_CALL) or x.group(x86.X86_GRP_RET): use = x; break
        if kind == 'slot0' and tn in fam(use.regs_access()[1]): pass   # [T] overwrote T
        elif kind == 'slot0':   # after [T]: T must not be used again (other than [T]) before it dies
            for x in ins[ins.index(use) + 1:24]:
                mems = [op.mem for op in x.operands if op.type == x86.X86_OP_MEM]
                r, w = fam(x.regs_access()[0]), fam(x.regs_access()[1])
                if any(mm.base == t or mm.index == t for mm in mems) and not all(mm.base == t and not mm.index and not mm.disp for mm in mems if mm.base == t or mm.index == t):
                    kind = 'slot0-reused'; break
                if any(mm.base == t for mm in mems): r.discard(tn)
                if tn in r: kind = 'slot0-reused'; break
                if tn in w: break
                if x.group(x86.X86_GRP_JUMP) or x.group(x86.X86_GRP_CALL) or x.group(x86.X86_GRP_RET): break
        res[kind] += 1
        if kind == 'slot0': slot0.append((ins[0].address, use.address))
        if kind == 'slot0-reused': print('REUSED', hex(ins[0].address))
        if verbose and kind == 'other':
            print(f'{ins[0].address:#x}', ' ; '.join(f'{x.mnemonic} {x.op_str}' for x in ins[1:5]))
print(dict(res))
if '-rva' in sys.argv:
    rv = [a - 0x140000000 for a, _ in slot0]
    for k in range(0, len(rv), 8): print('    ' + ', '.join(f'{x:#08x}' for x in rv[k:k + 8]) + ',')
gaps = collections.Counter(u - a for a, u in slot0)
print('slot0 load->use distance:', dict(gaps))
