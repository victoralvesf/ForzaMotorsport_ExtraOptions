# Memoria comprometida de um .dmp por alocacao (AllocationBase): total MEM_PRIVATE/MAPPED/IMAGE, histograma
# por tamanho de alocacao e por protecao, e as maiores alocacoes (com o modulo, se cair num).
# Uso: python tools/dmp_meminfo.py <dmp> [top=25]
# Dois dumps: python tools/dmp_meminfo.py <a.dmp> <b.dmp>  -> diferenca por faixa de tamanho/protecao
import sys, logging, collections
logging.disable(logging.CRITICAL)
from minidump.minidumpfile import MinidumpFile

MB = 1 << 20

def load(path):
    mf = MinidumpFile.parse(path)
    allocs = collections.defaultdict(lambda: [0, set(), None])   # base -> [commit, protects, type]
    tot = collections.Counter()
    for e in mf.memory_info.infos:
        st = getattr(e.State, 'name', str(e.State))
        if st != 'MEM_COMMIT': continue
        ty = getattr(e.Type, 'name', str(e.Type))
        pr = getattr(e.Protect, 'name', str(e.Protect))
        a = allocs[e.AllocationBase]
        a[0] += e.RegionSize; a[1].add(pr); a[2] = ty
        tot[ty] += e.RegionSize
    mods = [(m.baseaddress, m.baseaddress + m.size, m.name.split('\\')[-1]) for m in mf.modules.modules]
    return allocs, tot, mods

def bucket(sz):
    for lim, name in ((64 << 10, '<=64K'), (256 << 10, '<=256K'), (1 << 20, '<=1M'), (2 << 20, '<=2M'), (4 << 20, '<=4M'),
                      (16 << 20, '<=16M'), (64 << 20, '<=64M'), (256 << 20, '<=256M'), (1 << 30, '<=1G')):
        if sz <= lim: return name
    return '>1G'

def hist(allocs, ty='MEM_PRIVATE'):
    h = collections.defaultdict(lambda: [0, 0])
    for base, (sz, prs, t) in allocs.items():
        if t != ty: continue
        k = (bucket(sz), '/'.join(sorted(prs)))
        h[k][0] += 1; h[k][1] += sz
    return h

def main():
    a1, t1, mods = load(sys.argv[1])
    if len(sys.argv) > 2 and sys.argv[2].endswith('.dmp'):
        a2, t2, _ = load(sys.argv[2])
        for ty in sorted(set(t1) | set(t2)):
            print(f'{ty}: {t1[ty]/MB:.0f} -> {t2[ty]/MB:.0f} MiB ({(t2[ty]-t1[ty])/MB:+.0f})')
        h1, h2 = hist(a1), hist(a2)
        rows = []
        for k in set(h1) | set(h2):
            c1, s1 = h1.get(k, [0, 0]); c2, s2 = h2.get(k, [0, 0])
            rows.append((s2 - s1, k, c1, c2, s1, s2))
        rows.sort(reverse=True)
        print('MEM_PRIVATE por (tamanho da alocacao, protecao): delta | alocacoes a -> b | MiB a -> b')
        for d, k, c1, c2, s1, s2 in rows:
            if abs(d) >= MB or c1 != c2:
                print(f'  {d/MB:+8.0f} MiB  {k[0]:>7} {k[1]:<40} {c1:6} -> {c2:6}  {s1/MB:8.0f} -> {s2/MB:8.0f}')
        return
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    for ty, v in t1.items(): print(f'{ty}: {v/MB:.0f} MiB')
    print('MEM_PRIVATE por (tamanho da alocacao, protecao): alocacoes, MiB')
    for k, (c, s) in sorted(hist(a1).items(), key=lambda kv: -kv[1][1]):
        print(f'  {k[0]:>7} {k[1]:<40} {c:6} {s/MB:8.0f}')
    print('maiores alocacoes:')
    def sym(v):
        for lo, hi, n in mods:
            if lo <= v < hi: return n
        return ''
    for base, (sz, prs, t) in sorted(a1.items(), key=lambda kv: -kv[1][0])[:top]:
        print(f'  {base:#014x} {sz/MB:8.1f} MiB {t:<12} {"/".join(sorted(prs)):<40} {sym(base)}')

main()
