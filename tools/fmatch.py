"""Fuzzy function matching FH2 -> FM5 and site mapping by instruction alignment.
usage: fmatch.py FH2.exe FM5.exe sites.json
"""
import sys, re, json, pickle, os, difflib
from collections import Counter, defaultdict
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
sys.path.insert(0, os.path.dirname(__file__))
from sigfind import PE

md = Cs(CS_ARCH_X86, CS_MODE_64)
HEX = re.compile(r'0x[0-9a-f]+')
N = 4

def disasm(pe, a, b):
    return list(md.disasm_lite(pe.read(a, b - a), a))

def feats(ins):
    toks = [m for _, _, m, _ in ins]
    consts = Counter()
    for _, _, m, op in ins:
        if 'rip' in op or m in ('call', 'jmp') or m.startswith('j'): continue
        for h in HEX.findall(op):
            v = int(h, 16)
            if v > 8: consts[v] += 1
    grams = Counter(tuple(toks[i:i + N]) for i in range(len(toks) - N + 1))
    return toks, grams, consts

def score(fa, fb):
    ga, gb = fa[1], fb[1]
    inter = sum((ga & gb).values()); uni = sum((ga | gb).values()) or 1
    ca, cb = fa[2], fb[2]
    ci = sum((ca & cb).values()); cu = sum((ca | cb).values()) or 1
    return 0.75 * inter / uni + 0.25 * ci / cu

def index(pe, cache):
    if os.path.exists(cache): return pickle.load(open(cache, 'rb'))
    F = {}; inv = defaultdict(list)
    for a, b in pe.funcs:
        if b - a < 8 or b - a > 0x20000: continue
        ins = disasm(pe, a, b)
        if len(ins) < 4: continue
        toks, grams, consts = feats(ins)
        F[a] = (b, len(ins), grams, consts)
        for g in grams: inv[hash(g)].append(a)
    pickle.dump((F, dict(inv)), open(cache, 'wb'), protocol=4)
    return F, dict(inv)

def best_matches(fh, fstart, fend, F, inv, k=3):
    ins = disasm(fh, fstart, fend); fa = feats(ins)
    cand = Counter()
    for g in fa[1]:
        lst = inv.get(hash(g), [])
        if 0 < len(lst) < 400:
            for c in lst: cand[c] += 1
    res = []
    for c, _ in cand.most_common(300):
        b, n, grams, consts = F[c]
        if not (0.5 * len(ins) <= n <= 2 * len(ins)): continue
        res.append((score(fa, (None, grams, consts)), c, b))
    res.sort(reverse=True)
    return ins, res[:k]

def map_site(fh, fm, ins_a, mstart, mend, site):
    ins_b = disasm(fm, mstart, mend)
    ta = [m + ' ' + HEX.sub('#', op) for _, _, m, op in ins_a]
    tb = [m + ' ' + HEX.sub('#', op) for _, _, m, op in ins_b]
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    ia = next(i for i, x in enumerate(ins_a) if x[0] <= site < x[0] + x[1])
    delta = site - ins_a[ia][0]
    for tag, a0, a1, b0, b1 in sm.get_opcodes():
        if a0 <= ia < a1 and tag == 'equal':
            return ins_b[b0 + ia - a0][0] + delta, 'exato'
        if a0 <= ia < a1 and tag == 'replace' and (a1 - a0) == (b1 - b0):
            return ins_b[b0 + ia - a0][0] + delta, 'substituido'
    return None, 'nao-alinhado'

if __name__ == '__main__':
    fh, fm = PE(sys.argv[1]), PE(sys.argv[2])
    sites = json.load(open(sys.argv[3]))
    cache = os.path.join(os.path.dirname(sys.argv[3]), 'fm5_index.pkl')
    F, inv = index(fm, cache)
    out = {}
    for k, (rva, n) in sites.items():
        f = fh.func(rva)
        if not f:
            print(f'{k:>6} {rva:#09x}: dado (fora de funcao)'); out[k] = None; continue
        ins, top = best_matches(fh, f[0], f[1], F, inv)
        if not top:
            print(f'{k:>6} {rva:#09x}: sem candidato'); out[k] = None; continue
        s, ms, me = top[0]
        m, how = map_site(fh, fm, ins, ms, me, rva)
        second = top[1][0] if len(top) > 1 else 0
        print(f'{k:>6} fh2 {rva:#09x} func {f[0]:#x} -> fm5 func {ms:#x} score {s:.2f} '
              f'(2o {second:.2f}) site {m and hex(m)} [{how}]')
        out[k] = dict(fh2_func=hex(f[0]), fm5_func=hex(ms), score=round(s, 3), second=round(second, 3),
                      fm5_site=m and hex(m), how=how)
    json.dump(out, open(sys.argv[3].replace('.json', '_match.json'), 'w'), indent=1)
