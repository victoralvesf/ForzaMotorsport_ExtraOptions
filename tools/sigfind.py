"""Find FH2 patch sites in the FM5 exe by masked byte signatures.
usage: sigfind.py FH2.exe FM5.exe sites.json [before after]
"""
import sys, struct, re, json, bisect
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM, CS_OP_MEM
from capstone.x86 import X86_REG_RIP

class PE:
    def __init__(s, p):
        d = s.d = open(p, 'rb').read()
        e = struct.unpack_from('<I', d, 0x3c)[0]; opt = e + 24
        s.base = struct.unpack_from('<Q', d, opt + 24)[0]
        n = struct.unpack_from('<H', d, e + 6)[0]; so = struct.unpack_from('<H', d, e + 20)[0]
        s.secs = [struct.unpack_from('<8sIIII', d, opt + so + i * 40) for i in range(n)]
        er, es = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
        x = s.r2o(er); s.funcs = []
        for i in range(es // 12):
            b, en, u = struct.unpack_from('<III', d, x + i * 12)
            if b: s.funcs.append((b, en))
        s.funcs.sort(); s.fstarts = [f[0] for f in s.funcs]
        t = [x for x in s.secs if x[0].rstrip(b'\0') == b'.text'][0]
        s.text_rva, s.text_off, s.text_len = t[2], t[4], t[3]
        s.text = d[t[4]:t[4] + t[3]]
    def r2o(s, rva):
        for nm, vs, va, rs, ro in s.secs:
            if va <= rva < va + rs: return rva - va + ro
    def read(s, rva, n): x = s.r2o(rva); return s.d[x:x + n]
    def func(s, rva):
        i = bisect.bisect_right(s.fstarts, rva) - 1
        if i >= 0 and s.funcs[i][0] <= rva < s.funcs[i][1]: return s.funcs[i]

md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True

def masked(pe, start, end):
    """Return list of (rva, bytes, mask) for instructions in [start, end)."""
    code = pe.read(start, end - start); out = []
    for ins in md.disasm(code, start):
        b = bytearray(ins.bytes); m = bytearray(b'\xff' * len(b))
        dsz = ins.disp_size if hasattr(ins, 'disp_size') else 0
        mask_disp = False
        for op in ins.operands:
            if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP: mask_disp = True
        if mask_disp and ins.disp_offset:
            for k in range(ins.disp_offset, ins.disp_offset + ins.disp_size): m[k] = 0
        if ins.group(7) or ins.group(2) or ins.group(1):  # rel branch / call / jump
            if ins.imm_offset and any(op.type == CS_OP_IMM for op in ins.operands):
                for k in range(ins.imm_offset, ins.imm_offset + ins.imm_size): m[k] = 0
        out.append((ins.address, bytes(b), bytes(m), f'{ins.mnemonic} {ins.op_str}'))
    return out

def to_regex(insns):
    pat = b''
    for _, b, m, _ in insns:
        for x, y in zip(b, m): pat += re.escape(bytes([x])) if y else b'.'
    return re.compile(pat, re.S)

def search(fm, insns, site_rva):
    rx = to_regex(insns); first = insns[0][0]
    return [fm.text_rva + mo.start() + (site_rva - first) for mo in rx.finditer(fm.text)]

def find(fh, fm, site, n, before=0x30, after=0x30):
    f = fh.func(site)
    lo = max(f[0], site - before) if f else site - before
    hi = min(f[1], site + n + after) if f else site + n + after
    # align lo to an instruction boundary by disassembling from function start
    start = f[0] if f else lo
    all_ins = masked(fh, start, hi)
    win = [i for i in all_ins if i[0] >= lo]
    best = None
    # shrink the window until we get exactly one hit (prefer widest unique window)
    for trim in range(0, len(win)):
        for a, z in ((trim, len(win)), (0, len(win) - trim)):
            w = win[a:z]
            if not w or not (w[0][0] <= site < w[-1][0] + len(w[-1][1])): continue
            hits = search(fm, w, site)
            if len(hits) == 1: return hits, w, f
            if best is None or (hits and len(hits) < len(best[0])): best = (hits, w)
    return (best[0] if best else []), (best[1] if best else win), f

if __name__ == '__main__':
    fh, fm = PE(sys.argv[1]), PE(sys.argv[2])
    sites = json.load(open(sys.argv[3]))
    res = {}
    for k, (rva, n) in sites.items():
        hits, w, f = find(fh, fm, rva, n)
        tag = 'UNICO' if len(hits) == 1 else f'{len(hits)} hits'
        print(f'{k:>6} fh2 {rva:#09x} len {n:#05x} func {f[0]:#x}-{f[1]:#x} -> {tag} '
              f'{[hex(h) for h in hits[:4]]} (janela {len(w)} instr)')
        res[k] = [hex(h) for h in hits[:8]]
    json.dump(res, open(sys.argv[3].replace('.json', '_fm5.json'), 'w'), indent=1)
